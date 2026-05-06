// Test functions for testing the later C API.
// Moved from the latertest/ helper package; uses only R's C API (no cpp4r/Rcpp/cpp11).
// This file is compiled only as part of the main 'later' package for testing purposes.
//
// NOTE: We do NOT include <later_api.h> here because that header contains a
// static LaterInitializer that calls R_GetCCallable("later", ...), which would
// create a circular dependency when compiled inside the later package itself.
// Instead, we forward-declare the internal C callables directly.

#include "r_api.h"
#include "later.h"
#include <cstdint>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#else
#include <windows.h>
#endif

#include "interrupt.h"

// LATER_H_API_VERSION is defined in inst/include/later_api.h; mirror it here.
#define LATER_H_API_VERSION 3

// Forward declarations for the C callables defined in later.cpp / fd.cpp.
extern "C" int apiVersion();
extern "C" uint64_t execLaterNative2(void (*func)(void*), void* data, double delaySecs, int loop_id);

// ============================================================================
// test-c-api.R: API version checks
// ============================================================================

int later_dll_api_version() {
  return apiVersion();
}

int later_h_api_version() {
  return LATER_H_API_VERSION;
}

// ============================================================================
// test-later-fd.R: FD API test
// ============================================================================

namespace {
  void fd_test_func(int* /*value*/, void* /*data*/) {
    // Empty callback for testing
  }
}

#ifndef _WIN32
#include <poll.h>
#endif

extern "C" int execLaterFdNative(void (*func)(int *, void *), void *data, int num_fds, struct pollfd *fds, double timeoutSecs, int loop_id);

int testfd() {
  execLaterFdNative(fd_test_func, nullptr, 0, nullptr, 0.0, 0);
  return 0;
}

// ============================================================================
// BackgroundTask: inline implementation (avoids including later_api.h)
// ============================================================================

class BackgroundTask {
public:
  BackgroundTask() {}
  virtual ~BackgroundTask() {}

  void begin() {
#ifndef _WIN32
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    pthread_create(&t, &attr, BackgroundTask::task_main, this);
    pthread_attr_destroy(&attr);
#else
    HANDLE hThread = ::CreateThread(NULL, 0, BackgroundTask::task_main_win, this, 0, NULL);
    ::CloseHandle(hThread);
#endif
  }

protected:
  virtual void execute() = 0;
  virtual void complete() = 0;

private:
  static void* task_main(void* data) {
    BackgroundTask* task = reinterpret_cast<BackgroundTask*>(data);
    task->execute();
    execLaterNative2(&BackgroundTask::result_callback, task, 0, 0 /*GLOBAL_LOOP*/);
    return NULL;
  }

#ifdef _WIN32
  static DWORD WINAPI task_main_win(LPVOID lpParameter) {
    task_main(lpParameter);
    return 1;
  }
#endif

  static void result_callback(void* data) {
    BackgroundTask* task = reinterpret_cast<BackgroundTask*>(data);
    task->complete();
    delete task;
  }
};

// ============================================================================
// test-run_now.R: Background task test
// ============================================================================

class TestTask : public BackgroundTask {
  int _timeoutSecs;

public:
  explicit TestTask(int timeoutSecs) : _timeoutSecs(timeoutSecs) {}

protected:
  void execute() {
#ifndef _WIN32
    sleep(_timeoutSecs);
#else
    Sleep(_timeoutSecs * 1000);
#endif
  }

  void complete() {}
};

void launchBgTask(int secsToSleep) {
  (new TestTask(secsToSleep))->begin();
}

// ============================================================================
// test-run_now.R: Callback ordering test
// ============================================================================

namespace {
  void* max_seen = 0;

  void ordering_callback(void* data) {
    if (data < max_seen) {
      Rf_error("Bad ordering detected");
    }
    max_seen = data;
  }
}

void checkLaterOrdering() {
  max_seen = 0;
  for (size_t i = 0; i < 10000; i++) {
    execLaterNative2(ordering_callback, (void*)i, 0, 0 /*GLOBAL_LOOP*/);
  }
}

// ============================================================================
// test-run_now.R: C++ error/exception handling test
// ============================================================================

namespace {
  void error_callback(void* data) {
    int* v = (int*)data;
    int value = *v;
    delete v;

    if (value == 1) {
      throw std::runtime_error("This is a C++ exception.");

    } else if (value == 2) {
      // Throw an arbitrary object
      throw std::string();

    } else if (value == 3) {
      // Interrupt the interpreter
      Rf_onintr();

    } else if (value == 4) {
      // Calls R function which interrupts via R's C API
      SEXP e;
      PROTECT(e = Rf_lang1(Rf_install("r_interrupt")));
      Rf_eval(e, R_GlobalEnv);
      UNPROTECT(1);

    } else if (value == 5) {
      // Calls R function which calls stop() via R's C API
      SEXP e;
      PROTECT(e = Rf_lang1(Rf_install("r_error")));
      Rf_eval(e, R_GlobalEnv);
      UNPROTECT(1);

    } else if (value == 6) {
      // Calls the `r_error` function via R's C API
      SEXP e;
      PROTECT(e = Rf_lang1(Rf_install("r_error")));
      Rf_eval(e, R_GlobalEnv);
      UNPROTECT(1);
    }
  }
}

void cpp_error(int value) {
  int* v = new int(value);
  execLaterNative2(error_callback, v, 0, 0 /*GLOBAL_LOOP*/);
}

// ============================================================================
// test-promises.R: Promise task for async fibonacci
// ============================================================================

class PromiseTask : public BackgroundTask {
public:
  PromiseTask(SEXP resolve, SEXP reject)
      : resolve_sexp(resolve), reject_sexp(reject) {}

protected:
  virtual void execute() = 0;
  virtual double get_result() = 0;

  void complete() {
    SEXP result_sexp = PROTECT(Rf_ScalarReal(get_result()));
    SEXP resolve = static_cast<SEXP>(resolve_sexp);
    unwind_protect([resolve, result_sexp]() -> SEXP {
      return Rf_eval(Rf_lang2(resolve, result_sexp), R_GlobalEnv);
    });
    UNPROTECT(1);
  }

  PreservedSEXP resolve_sexp;
  PreservedSEXP reject_sexp;
};

namespace {
  long fib(long x) {
    if (x <= 2) {
      return 1;
    }
    return fib(x - 1) + fib(x - 2);
  }
}

class FibonacciTask : public PromiseTask {
public:
  FibonacciTask(SEXP resolve, SEXP reject, double x)
      : PromiseTask(resolve, reject), x(x), result(0) {}

  void execute() {
    result = fib((long)x);
  }

  double get_result() {
    return (double)result;
  }

private:
  double x;
  long result;
};

void asyncFib(SEXP resolve, SEXP reject, double x) {
  FibonacciTask* task = new FibonacciTask(resolve, reject, x);
  task->begin();
}

