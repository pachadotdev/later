#include <later_api.h>
#include <Rinternals.h>
#include <vector>

// MeanTask: computes the mean of a numeric vector on a background thread.
// This is the example from the "Using later from C++" vignette.
//
// The constructor (R thread) copies the input into a plain std::vector so the
// background thread never touches an R data structure.
// execute() (background thread) does the arithmetic.
// complete() (R thread) delivers the result.

class MeanTask : public later::BackgroundTask {
public:
  // Constructor used by asyncMean() — result is printed via Rprintf.
  explicit MeanTask(cpp4r::doubles vec)
      : inputVals(vec.begin(), vec.end()),
        callback_sexp(R_NilValue),
        result(0) {}

  // Constructor used by asyncMeanCb() — result is passed to an R callback.
  MeanTask(cpp4r::doubles vec, SEXP callback)
      : inputVals(vec.begin(), vec.end()),
        callback_sexp(callback),
        result(0) {
    R_PreserveObject(callback_sexp);
  }

protected:
  void execute() {
    double sum = 0;
    for (double val : inputVals) {
      sum += val;
    }
    result = sum / inputVals.size();
  }

  void complete() {
    if (callback_sexp == R_NilValue) {
      // As shown in the vignette: just print the result.
      Rprintf("Result is %f\n", result);
    } else {
      // Testable variant: invoke the R callback with the result.
      SEXP r = PROTECT(Rf_ScalarReal(result));
      SEXP call = PROTECT(Rf_lang2(callback_sexp, r));
      Rf_eval(call, R_GlobalEnv);
      UNPROTECT(2);
      R_ReleaseObject(callback_sexp);
    }
  }

private:
  std::vector<double> inputVals;
  SEXP callback_sexp;
  double result;
};

/* roxygen
@title Compute mean on a background thread (print result)
@param data numeric vector
@description Launches a background thread that computes the mean of \code{data}.
  The result is printed via \code{Rprintf} once the background thread finishes.
  This matches the example from the "Using later from C++" vignette.
@export
@examples
asyncMean(c(1, 2, 3))
later::run_now(2)
*/
[[cpp4r::register]]
void asyncMean(cpp4r::doubles data) {
  (new MeanTask(data))->begin();
}

/* roxygen
@title Compute mean on a background thread (callback result)
@param data numeric vector
@param callback an R function that will be called with the mean as its argument
@description Like \code{asyncMean} but delivers the result to an R callback
  instead of printing it. Useful for testing and for integrating with promises.
@export
@examples
result <- NULL
asyncMeanCb(c(1, 2, 3), function(x) result <<- x)
later::run_now(2)
result
*/
[[cpp4r::register]]
void asyncMeanCb(cpp4r::doubles data, SEXP callback) {
  (new MeanTask(data, callback))->begin();
}
