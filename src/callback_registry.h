#ifndef _CALLBACK_REGISTRY_H_
#define _CALLBACK_REGISTRY_H_

#include "r_api.h"
#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include "timestamp.h"
#include "optional.h"
#include "threadutils.h"

// Callback is an abstract class with two subclasses. The reason that there
// are two subclasses is because one of them is for C++ (std::function)
// callbacks, and the other is for R (cpp4r::function) callbacks. Because
// Callbacks can be created from either the main thread or a background
// thread, the top-level Callback class cannot contain any cpp4r objects --
// otherwise R objects could be allocated on a background thread, which will
// cause memory corruption.

class Callback {

public:
  virtual ~Callback() {};
  Callback(Timestamp when) : when(when) {};

  bool operator<(const Callback& other) const {
    return this->when < other.when ||
      (!(this->when > other.when) && this->callbackId < other.callbackId);
  }

  bool operator>(const Callback& other) const {
    return other < *this;
  }

  uint64_t getCallbackId() const {
    return callbackId;
  };

  virtual void invoke() const = 0;

  virtual SEXP rRepresentation() const = 0;

  Timestamp when;

protected:
  // Used to break ties when comparing to a callback that has precisely the same
  // timestamp
  uint64_t callbackId;
};


class StdFunctionCallback : public Callback {
public:
  StdFunctionCallback(Timestamp when, std::function<void (void)> func);

  void invoke() const {
    // See https://github.com/r-lib/later/issues/191 and https://github.com/r-lib/later/pull/241
    // C++ exceptions must NOT escape through R_UnwindProtect (which unwind_protect uses
    // internally). R_UnwindProtect installs an RCNTXT via begincontext(); if a C++
    // exception unwinds through it, endcontext() is never called, leaving a dangling
    // entry in R's context stack and causing memory corruption / segfault.
    // Convert any C++ exception to an Rf_error() call *inside* the lambda so it goes
    // through R's longjmp mechanism, which properly calls endcontext().
    unwind_protect([this]() -> SEXP {
      try {
        func();
      } catch (const std::exception& e) {
        Rf_error("%s", e.what());
      } catch (...) {
        Rf_error("C++ exception (unknown reason)");
      }
      return R_NilValue;
    });
  }

  SEXP rRepresentation() const;

private:
  std::function<void (void)> func;
};


class RFunctionCallback : public Callback {
public:
  RFunctionCallback(Timestamp when, SEXP func);

  void invoke() const {
    SEXP f = static_cast<SEXP>(func_sexp);
    unwind_protect([f]() -> SEXP {
      return Rf_eval(Rf_lang1(f), R_GlobalEnv);
    });
  }

  SEXP rRepresentation() const;

private:
  PreservedSEXP func_sexp;
};



typedef std::shared_ptr<Callback> Callback_sp;

template <typename T>
struct pointer_less_than {
  const bool operator()(const T a, const T b) const {
    return *a < *b;
  }
};


// Stores R function callbacks, ordered by timestamp.
class CallbackRegistry {
private:
  int id;

  // Most of the behavior of the registry is like a priority queue. However, a
  // std::priority_queue only allows access to the top element, and when we
  // cancel a callback or get a cpp4r::list representation, we need random
  // access, so we'll use a std::set.
  typedef std::set<Callback_sp, pointer_less_than<Callback_sp> > cbSet;
  // This is a priority queue of shared pointers to Callback objects. The
  // reason it is not a priority_queue<Callback> is because that can cause
  // objects to be copied on the wrong thread, and even trigger an R GC event
  // on the wrong thread. https://github.com/r-lib/later/issues/39
  cbSet queue;
  std::atomic<int> fd_waits{};
  Mutex* mutex;
  ConditionVariable* condvar;

public:
  // The CallbackRegistry must be given a Mutex and ConditionVariable when
  // initialized, because they are shared among the CallbackRegistry objects
  // and the CallbackRegistryTable; they serve as a global lock. Note that the
  // lifetime of these objects must be longer than the CallbackRegistry.
  CallbackRegistry(int id, Mutex* mutex, ConditionVariable* condvar);
  ~CallbackRegistry();

  int getId() const;

  // Add a function to the registry, to be executed at `secs` seconds in
  // the future (i.e. relative to the current time).
  uint64_t add(SEXP func, double secs);

  // Add a C function to the registry, to be executed at `secs` seconds in
  // the future (i.e. relative to the current time).
  uint64_t add(void (*func)(void*), void* data, double secs);

  bool cancel(uint64_t id);

  // The smallest timestamp present in the registry, if any.
  // Use this to determine the next time we need to pump events.
  Optional<Timestamp> nextTimestamp(bool recursive = true) const;

  // Is the registry completely empty? (including later_fd waits)
  bool empty() const;

  // Is anything ready to execute?
  bool due(const Timestamp& time = Timestamp(), bool recursive = true) const;

  // Pop and return a function to execute now.
  Callback_sp pop(const Timestamp& time = Timestamp());

  // Wait until the next available callback is ready to execute.
  bool wait(double timeoutSecs, bool recursive) const;

  // Return a List of items in the queue.
  SEXP list() const;

  // Increment and decrement the number of active later_fd waits
  void fd_waits_incr();
  void fd_waits_decr();

  // References to parent and children registries. These are used for
  // automatically running child loops. They should only be accessed and
  // modified from the main thread.
  std::shared_ptr<CallbackRegistry> parent;
  std::vector<std::shared_ptr<CallbackRegistry> > children;
};

#endif // _CALLBACK_REGISTRY_H_
