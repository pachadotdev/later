# Helper: flush the later event loop until empty or timeout expires.
run_until_empty <- function(timeout = 2) {
  later::run_now(timeout)
  while (!later::loop_empty()) {
    later::run_now(0.1)
  }
}

test_that("asyncMean computes the correct mean (basic case)", {
  result <- NULL
  asyncMeanCb(c(1.0, 2.0, 3.0), function(x) result <<- x)
  run_until_empty()
  expect_equal(result, 2.0)
})

test_that("asyncMean handles a single-element vector", {
  result <- NULL
  asyncMeanCb(42.0, function(x) result <<- x)
  run_until_empty()
  expect_equal(result, 42.0)
})

test_that("asyncMean handles a two-element vector", {
  result <- NULL
  asyncMeanCb(c(0.0, 1.0), function(x) result <<- x)
  run_until_empty()
  expect_equal(result, 0.5)
})

test_that("asyncMean result matches base R mean()", {
  set.seed(42)
  x <- runif(100)
  result <- NULL
  asyncMeanCb(x, function(v) result <<- v)
  run_until_empty()
  expect_equal(result, mean(x))
})

test_that("asyncMean callback is called exactly once", {
  calls <- 0L
  asyncMeanCb(c(1.0, 2.0), function(x) calls <<- calls + 1L)
  run_until_empty()
  expect_equal(calls, 1L)
})

test_that("multiple concurrent asyncMean calls all complete", {
  results <- vector("list", 3)
  asyncMeanCb(c(1.0, 2.0, 3.0), function(x) results[[1]] <<- x)
  asyncMeanCb(c(4.0, 5.0, 6.0), function(x) results[[2]] <<- x)
  asyncMeanCb(c(7.0, 8.0, 9.0), function(x) results[[3]] <<- x)
  # Poll until every result has arrived. loop_empty() cannot be used here:
  # it may return TRUE between two background threads finishing, i.e. after
  # the first callback fires but before the remaining threads have posted
  # their own callbacks via execLaterNative2.
  deadline <- proc.time()[["elapsed"]] + 5
  while (any(vapply(results, is.null, logical(1))) &&
         proc.time()[["elapsed"]] < deadline) {
    later::run_now(0.1)
  }
  expect_equal(results[[1]], 2.0)
  expect_equal(results[[2]], 5.0)
  expect_equal(results[[3]], 8.0)
})
