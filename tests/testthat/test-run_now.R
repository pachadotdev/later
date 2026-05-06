jitter <- 0.017 * 2 # Compensate for imprecision in system timer

test_that("run_now waits and returns FALSE if no tasks", {
  x <- system.time({
    result <- run_now(0.5)
  })
  expect_gte(as.numeric(x[["elapsed"]]), 0.5 - jitter)
  expect_identical(result, FALSE)

  x <- system.time({
    result <- run_now(3)
  })
  expect_gte(as.numeric(x[["elapsed"]]), 3 - jitter)
  expect_identical(result, FALSE)
})

test_that("run_now returns immediately after executing a task", {
  x <- system.time({
    later(~ {}, 0)
    result <- run_now(2)
  })
  expect_lt(as.numeric(x[["elapsed"]]), 0.25)
  expect_identical(result, TRUE)
})

test_that("run_now executes all scheduled tasks, not just one", {
  later(~ {}, 0)
  later(~ {}, 0)
  result1 <- run_now()
  result2 <- run_now()
  expect_identical(result1, TRUE)
  expect_identical(result2, FALSE)
})

test_that("run_now executes just one scheduled task, if requested", {
  result1 <- run_now()
  expect_identical(result1, FALSE)

  later(~ {}, 0)
  later(~ {}, 0)

  result2 <- run_now(all = FALSE)
  expect_identical(result2, TRUE)

  result3 <- run_now(all = FALSE)
  expect_identical(result3, TRUE)

  result4 <- run_now()
  expect_identical(result4, FALSE)
})

test_that("run_now doesn't go past a failed task", {
  later(~ stop("boom"), 0)
  later(~ {}, 0)
  expect_snapshot(error = TRUE, run_now())
  expect_true(run_now())
})

test_that("run_now wakes up when a background thread calls later()", {
  # Skip due to false positives on UBSAN
  skip_if(using_ubsan())

  # The background task sleeps
  later:::launchBgTask(1)

  x <- system.time({
    result <- run_now(3)
  })
  # Wait for up to 1.5 seconds (for slow systems)
  expect_lt(as.numeric(x[["elapsed"]]), 1.5)
  expect_true(result)
})

test_that("When callbacks have tied timestamps, they respect order of creation", {
  # Skip due to false positives on UBSAN
  skip_if(using_ubsan())

  expect_snapshot(testCallbackOrdering())

  later:::checkLaterOrdering()
  while (!loop_empty()) {
    run_now(0.1)
  }
})


test_that("Callbacks cannot affect the caller", {
  # This is based on a pattern used in the callCC function. Normally, simply
  # touching `throw` will cause the expression to be evaluated and f() to return
  # early. (This test does not involve later.)
  f <- function() {
    delayedAssign("throw", return(100))
    g <- function() {
      throw
    }
    g()
    return(200)
  }
  expect_equal(f(), 100)

  # When later runs callbacks, it wraps the call in R_ToplevelExec(), which
  # creates a boundary on the call stack that the early return can't cross.
  f <- function() {
    delayedAssign("throw", return(100))
    later(function() {
      throw
    })

    run_now(1)
    return(200)
  }
  # jcheng 2024-10-24: Apparently this works now, maybe because having
  # RCPP_USING_UNWIND_PROTECT means we don't need to use R_ToplevelExec to call
  # callbacks?
  # expect_error(f())
  expect_identical(f(), 100)

  # In this case, f() should return normally, and then when g() causes later to
  # run the callback with `throw`, it should be an error -- there's no function
  # to return from because it (f()) already returned.
  f <- function() {
    delayedAssign("throw", return(100))
    later(function() {
      throw
    })
    return(200)
  }
  g <- function() {
    run_now(2)
  }
  expect_equal(f(), 200)
  expect_snapshot(error = TRUE, g())
})


test_that("interrupt and exception handling, R", {
  # =======================================================
  # Errors and interrupts in R callbacks
  # =======================================================

  # R error
  error_obj <- FALSE
  tryCatch(
    {
      later(function() {
        stop("oopsie")
      })
      run_now()
    },
    error = function(e) {
      error_obj <<- e
    }
  )
  expect_true(grepl("oopsie", error_obj$message))

  # interrupt
  interrupted <- FALSE
  tryCatch(
    {
      later(function() {
        rlang::interrupt()
        Sys.sleep(100)
      })
      run_now()
    },
    interrupt = function(e) {
      interrupted <<- TRUE
    }
  )
  expect_true(interrupted)
})

test_that("interrupt and exception handling, C++", {
  # Skip as cpp_error(4) test seen producing error on some platforms on rhub
  skip_on_cran()
  # Skip due to false positives on UBSAN
  skip_if(using_ubsan())
  # Skip on Windows i386 because of known bad behavior
  if (R.version$os == "mingw32" && R.version$arch == "i386") {
    skip("C++ exceptions in later callbacks are known bad on Windows i386")
  }

  # =======================================================
  # Exceptions in C++ callbacks
  # =======================================================

  # In these tests, in C++, later schedules a C++ callback in which an
  # exception is thrown or interrupt occurs.
  #
  # Some of these callbacks in turn call R functions.

  # later:::cpp_error() searches in the global environment for these R functions, so we
  # need to define them there.
  .GlobalEnv$r_interrupt <- function() {
    rlang::interrupt()
  }
  .GlobalEnv$r_error <- function() {
    stop("oopsie")
  }
  on.exit(rm(r_interrupt, r_error, envir = .GlobalEnv), add = TRUE)

  errored <- FALSE
  tryCatch(
    {
      later:::cpp_error(1)
      run_now(Inf)
    },
    error = function(e) errored <<- TRUE
  )
  expect_true(errored)

  errored <- FALSE
  tryCatch(
    {
      later:::cpp_error(2)
      run_now(-1)
    },
    error = function(e) errored <<- TRUE
  )
  expect_true(errored)

  errored <- FALSE
  tryCatch(
    {
      later:::cpp_error(5)
      run_now()
    },
    error = function(e) errored <<- TRUE
  )
  expect_true(errored)

  errored <- FALSE
  tryCatch(
    {
      later:::cpp_error(6)
      run_now()
    },
    error = function(e) errored <<- TRUE
  )
  expect_true(errored)

  interrupted <- FALSE
  tryCatch(
    {
      later:::cpp_error(3)
      run_now()
    },
    interrupt = function(e) interrupted <<- TRUE
  )
  expect_true(interrupted)

  interrupted <- FALSE
  tryCatch(
    {
      later:::cpp_error(4)
      run_now()
    },
    interrupt = function(e) interrupted <<- TRUE
  )
  expect_true(interrupted)
})
