test_that("header and DLL API versions match", {
  expect_identical(later:::later_dll_api_version(), later:::later_h_api_version())
})

test_that("logLevel works", {
  current <- later:::logLevel()
  expect_true(current %in% c("OFF", "ERROR", "WARN", "INFO", "DEBUG"))

  previous <- later:::logLevel("DEBUG")
  expect_equal(previous, current)
  expect_equal(later:::logLevel(), "DEBUG")

  expect_equal(later:::logLevel(current), "DEBUG")
  expect_equal(later:::logLevel(), current)
})
