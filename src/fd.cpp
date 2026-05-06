#include "fd.h"
#include "r_api.h"
#include <unistd.h>
#include <cstdlib>
#include <atomic>
#include <memory>
#include "tinycthread.h"
#include "later.h"
#include "callback_registry_table.h"

class ThreadArgs {
public:
  ThreadArgs(
    int num_fds,
    struct pollfd *fds,
    double timeout,
    int loop,
    CallbackRegistryTable& table
  )
    : timeout(createTimestamp(timeout)),
      active(std::make_shared<std::atomic<bool>>(true)),
      fds(std::vector<struct pollfd>(fds, fds + num_fds)),
      results(std::vector<int>(num_fds)),
      loop(loop),
      registry(table.getRegistry(loop)) {

    if (registry == nullptr)
      throw std::runtime_error("CallbackRegistry does not exist.");

    registry->fd_waits_incr();
  }

  ThreadArgs(
    SEXP func,
    int num_fds,
    struct pollfd *fds,
    double timeout,
    int loop,
    CallbackRegistryTable& table
  ) : ThreadArgs(num_fds, fds, timeout, loop, table) {
    callback_sexp = func;
  }

  ThreadArgs(
    void (*func)(int *, void *),
    void *data,
    int num_fds,
    struct pollfd *fds,
    double timeout,
    int loop,
    CallbackRegistryTable& table
  ) : ThreadArgs(num_fds, fds, timeout, loop, table) {
    callback_native = std::bind(func, std::placeholders::_1, data);
  }

  ~ThreadArgs() {
    registry->fd_waits_decr();
  }

  Timestamp timeout;
  std::shared_ptr<std::atomic<bool>> active;
  PreservedSEXP callback_sexp;
  std::function<void (int *)> callback_native = nullptr;
  std::vector<struct pollfd> fds;
  std::vector<int> results;
  const int loop;

private:
  std::shared_ptr<CallbackRegistry> registry;

  static Timestamp createTimestamp(double timeout) {
    if (timeout > 3e10) {
      timeout = 3e10; // "1000 years ought to be enough for anybody" --Bill Gates
    } else if (timeout < 0) {
      timeout = 1; // curl_multi_timeout() uses -1 to denote a default we set at 1s
    }
    return Timestamp(timeout);
  }

};

static void later_callback(void *arg) {

  ASSERT_MAIN_THREAD()

  std::unique_ptr<ThreadArgs> args(static_cast<ThreadArgs *>(arg));
  bool still_active = true;
  // atomic compare_exchange_strong:
  // if args->active is true, it is changed to false (so future requests to fd_cancel return false)
  // if args->active is false (cancelled), still_active is changed to false
  args->active->compare_exchange_strong(still_active, false);
  if (!still_active)
    return;
  if (static_cast<SEXP>(args->callback_sexp) != R_NilValue) {
    int n = static_cast<int>(args->results.size());
    SEXP results_sexp = PROTECT(Rf_allocVector(LGLSXP, n));
    for (int i = 0; i < n; i++) {
      LOGICAL(results_sexp)[i] =
        args->results[i] == 0 ? FALSE :
        (args->results[i] == NA_INTEGER ? NA_LOGICAL : TRUE);
    }
    SEXP cb = static_cast<SEXP>(args->callback_sexp);
    // Release args first (runs fd_waits_decr) before calling into R, so that
    // fd_waits state is correct even if the R callback errors.
    SEXP results_copy = results_sexp; // keep reference alive
    (void)results_copy;
    args.reset();
    unwind_protect([cb, results_sexp]() -> SEXP {
      return Rf_eval(Rf_lang2(cb, results_sexp), R_GlobalEnv);
    });
    UNPROTECT(1);
  } else {
    args->callback_native(args->results.data());
  }

}

// CONSIDER: if necessary to add method for HANDLES on Windows. Would be different code to SOCKETs.
// TODO: implement re-usable background thread.
static int wait_thread(void *arg) {

  tct_thrd_detach(tct_thrd_current());

  std::unique_ptr<ThreadArgs> args(static_cast<ThreadArgs *>(arg));

  int ready;
  double waitFor = std::fmax(args->timeout.diff_secs(Timestamp()), 0);
  do {
    // Never wait for longer than ~1 second so we can check for cancellation
    waitFor = std::fmin(waitFor, 1.024);
    ready = LATER_POLL_FUNC(args->fds.data(), static_cast<LATER_NFDS_T>(args->fds.size()), static_cast<int>(waitFor * 1000));
    if (!args->active->load()) return 1;
    if (ready) break;
  } while ((waitFor = args->timeout.diff_secs(Timestamp())) > 0);

  if (ready > 0) {
    for (std::size_t i = 0; i < args->fds.size(); i++) {
      (args->results)[i] = (args->fds)[i].revents == 0 ? 0 : (args->fds)[i].revents & (POLLIN | POLLOUT) ? 1: NA_INTEGER;
    }
  } else if (ready < 0) {
    std::fill(args->results.begin(), args->results.end(), NA_INTEGER);
  }

  int loop_id = args->loop;
  callbackRegistryTable.scheduleCallback(later_callback, static_cast<void *>(args.release()), 0, loop_id);

  return 0;

}

static SEXP execLater_fd_impl(SEXP callback, int num_fds, struct pollfd *fds, double timeout, int loop_id) {

  std::unique_ptr<ThreadArgs> args(new ThreadArgs(callback, num_fds, fds, timeout, loop_id, callbackRegistryTable));
  std::shared_ptr<std::atomic<bool>> active = args->active;
  tct_thrd_t thr;

  if (tct_thrd_create(&thr, &wait_thread, static_cast<void *>(args.release())) != tct_thrd_success)
    Rf_error("Thread creation failed");

  // Wrap the shared_ptr<atomic<bool>> in an external pointer so R can hold it.
  std::shared_ptr<std::atomic<bool>>* pActive =
    new std::shared_ptr<std::atomic<bool>>(active);
  SEXP xptr = PROTECT(R_MakeExternalPtr(
    static_cast<void*>(pActive), R_NilValue, R_NilValue
  ));
  R_RegisterCFinalizerEx(
    xptr,
    [](SEXP x) {
      delete static_cast<std::shared_ptr<std::atomic<bool>>*>(R_ExternalPtrAddr(x));
      R_ClearExternalPtr(x);
    },
    TRUE
  );
  UNPROTECT(1);
  return xptr;

}

// native version
static int execLater_fd_native(void (*func)(int *, void *), void *data, int num_fds, struct pollfd *fds, double timeout, int loop_id) {

  std::unique_ptr<ThreadArgs> args(new ThreadArgs(func, data, num_fds, fds, timeout, loop_id, callbackRegistryTable));
  tct_thrd_t thr;

  return tct_thrd_create(&thr, &wait_thread, static_cast<void *>(args.release())) != tct_thrd_success;

}

SEXP execLater_fd(SEXP callback, SEXP readfds, SEXP writefds,
                  SEXP exceptfds, SEXP timeoutSecs, SEXP loop_id) {

  const int rfds = Rf_length(readfds);
  const int wfds = Rf_length(writefds);
  const int efds = Rf_length(exceptfds);
  const int num_fds = rfds + wfds + efds;
  const double timeout = num_fds ? Rf_asReal(timeoutSecs) : 0;
  const int loop = INTEGER(loop_id)[0];

  std::vector<struct pollfd> pollfds;
  pollfds.reserve(num_fds);
  struct pollfd pfd;

  for (int i = 0; i < rfds; i++) {
    pfd.fd = INTEGER(readfds)[i];
    pfd.events = POLLIN;
    pfd.revents = 0;
    pollfds.push_back(pfd);
  }
  for (int i = 0; i < wfds; i++) {
    pfd.fd = INTEGER(writefds)[i];
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pollfds.push_back(pfd);
  }
  for (int i = 0; i < efds; i++) {
    pfd.fd = INTEGER(exceptfds)[i];
    pfd.events = 0;
    pfd.revents = 0;
    pollfds.push_back(pfd);
  }

  return execLater_fd_impl(callback, num_fds, pollfds.data(), timeout, loop);

}

SEXP fd_cancel(SEXP xptr) {

  auto* pActive = static_cast<std::shared_ptr<std::atomic<bool>>*>(R_ExternalPtrAddr(xptr));
  if (!pActive) return Rf_ScalarLogical(FALSE);

  bool cancelled = true;
  // atomic compare_exchange_strong:
  // if *active is true, *active is changed to false (successful cancel)
  // if *active is false (already run or cancelled), cancelled is changed to false
  (*pActive)->compare_exchange_strong(cancelled, false);

  return Rf_ScalarLogical(cancelled ? TRUE : FALSE);

}

// Schedules a C function that takes a pointer to an integer array (provided by
// this function when calling back) and a void * argument, to execute on file
// descriptor readiness. Returns 0 upon success and 1 if creating the wait
// thread failed. NOTE: this is different to execLaterNative2() which returns 0
// on failure.
extern "C" int execLaterFdNative(void (*func)(int *, void *), void *data, int num_fds, struct pollfd *fds, double timeoutSecs, int loop_id) {
  ensureInitialized();
  return execLater_fd_native(func, data, num_fds, fds, timeoutSecs, loop_id);
}
