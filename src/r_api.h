#ifndef _LATER_R_API_H_
#define _LATER_R_API_H_

// Minimal R interop utilities that replace cpp4r.
// Provides: unwind_exception, unwind_protect, unwind_protect_void,
//           check_user_interrupt, PreservedSEXP.
//
// Must be included before any other R headers.
#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Error.h>
#include <R_ext/Random.h>
#include <R_ext/RS.h>
#include <R_ext/Visibility.h>

// ============================================================================
// unwind_exception
// ============================================================================

// Exception type used to carry R's unwind continuation token through C++ stack
// unwinding. This allows C++ destructors to run when R longjmps (e.g. on error
// or interrupt). The token must eventually be passed to R_ContinueUnwind() to
// resume the R-side unwind, OR the exception can be silently swallowed if the
// caller is at a top-level boundary (e.g. an R input handler).
struct unwind_exception {
  SEXP token;
  explicit unwind_exception(SEXP t) : token(t) {}
};

// ============================================================================
// unwind_protect
// ============================================================================

namespace detail {

struct unwind_clean_data {
  bool jumped;
  SEXP token;
};

inline void unwind_cleanfun(void* data, Rboolean jump) {
  if (jump) {
    // R is about to longjmp. Throw a C++ exception instead so that C++
    // destructors run on the way up, then the exception is caught at the
    // boundary and (optionally) the R unwind is continued.
    unwind_clean_data* ud = static_cast<unwind_clean_data*>(data);
    ud->jumped = true;
    throw unwind_exception(ud->token);
  }
}

template<typename F>
struct unwind_fun_data {
  F& f;
  SEXP result;
};

template<typename F>
SEXP unwind_fun(void* data) {
  unwind_fun_data<F>* d = static_cast<unwind_fun_data<F>*>(data);
  d->result = d->f();
  return R_NilValue;
}

} // namespace detail

// Wrap a callable that returns SEXP and may trigger R longjmps.
// If R longjmps during f(), the longjmp is intercepted and re-raised as a
// C++ unwind_exception so that all C++ destructors run.
template<typename F>
SEXP unwind_protect(F&& f) {
  SEXP token = PROTECT(R_MakeUnwindCont());

  detail::unwind_clean_data ud{false, token};
  detail::unwind_fun_data<F> fd{f, R_NilValue};

  R_UnwindProtect(
    detail::unwind_fun<F>, static_cast<void*>(&fd),
    detail::unwind_cleanfun, static_cast<void*>(&ud),
    token
  );

  UNPROTECT(1);

  if (ud.jumped) {
    // Shouldn't normally reach here (cleanfun throws), but be defensive.
    throw unwind_exception(token);
  }

  return fd.result;
}

// Convenience overload for void callables.
template<typename F>
void unwind_protect_void(F&& f) {
  unwind_protect([&]() -> SEXP {
    f();
    return R_NilValue;
  });
}

// ============================================================================
// check_user_interrupt
// ============================================================================

// Check for pending R user interrupt. If interrupted, converts the resulting
// R longjmp to a C++ unwind_exception so that C++ destructors run on the way
// up. The exception will propagate until caught at a top-level boundary.
inline void check_user_interrupt() {
  unwind_protect_void([]() { R_CheckUserInterrupt(); });
}

// ============================================================================
// PreservedSEXP
// ============================================================================

// RAII wrapper for R SEXP objects that must be kept alive (preserved from GC)
// for the lifetime of the C++ object. Calls R_PreserveObject on construction
// and R_ReleaseObject on destruction.
//
// Non-copyable; supports move and assignment from raw SEXP.
class PreservedSEXP {
public:
  PreservedSEXP() : sexp_(R_NilValue), owned_(false) {}

  explicit PreservedSEXP(SEXP s) : sexp_(s), owned_(s != R_NilValue) {
    if (owned_) R_PreserveObject(sexp_);
  }

  ~PreservedSEXP() {
    if (owned_) R_ReleaseObject(sexp_);
  }

  // Non-copyable
  PreservedSEXP(const PreservedSEXP&) = delete;
  PreservedSEXP& operator=(const PreservedSEXP&) = delete;

  // Move constructor
  PreservedSEXP(PreservedSEXP&& other) noexcept
    : sexp_(other.sexp_), owned_(other.owned_) {
    other.owned_ = false;
  }

  // Assign from raw SEXP (releases previous, preserves new)
  PreservedSEXP& operator=(SEXP s) {
    if (owned_) R_ReleaseObject(sexp_);
    sexp_ = s;
    owned_ = (s != R_NilValue);
    if (owned_) R_PreserveObject(sexp_);
    return *this;
  }

  operator SEXP() const { return sexp_; }

private:
  SEXP sexp_;
  bool owned_;
};

#endif // _LATER_R_API_H_
