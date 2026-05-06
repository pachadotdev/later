test_that("later C++ BackgroundTask class works with promises", {

  skip_if_not_installed("promises")

  # test that resolve works
  result <- 0
  promises::promise(function(resolve, reject) {
    later:::asyncFib(resolve, reject, 3)
  }) |>
    promises::then(\(x) {
      result <<- x
    })

  expect_identical(result, 0)
  run_now(1)
  while (!loop_empty()) {
    run_now(0.1)
  }
  expect_identical(result, 2)

  # test that reject works (swap resolve/reject)
  err_result <- 0
  promises::promise(function(resolve, reject) {
    later:::asyncFib(reject, resolve, 6)
  }) |>
    promises::catch(\(x) {
      err_result <<- x
    })

  expect_identical(err_result, 0)
  run_now(1)
  while (!loop_empty()) {
    run_now(0.1)
  }
  expect_identical(err_result, 8)
})
