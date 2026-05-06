# R wrappers for test C functions (previously in latertest/).
# These are used by tests/testthat/ and are not part of the public API.

later_dll_api_version <- function() {
	.Call(`_later_later_dll_api_version`)
}

later_h_api_version <- function() {
	.Call(`_later_later_h_api_version`)
}

testfd <- function() {
	.Call(`_later_testfd`)
}

launchBgTask <- function(secsToSleep) {
	invisible(.Call(`_later_launchBgTask`, secsToSleep))
}

checkLaterOrdering <- function() {
	invisible(.Call(`_later_checkLaterOrdering`))
}

cpp_error <- function(value) {
	invisible(.Call(`_later_cpp_error`, value))
}

asyncFib <- function(resolve, reject, x) {
	invisible(.Call(`_later_asyncFib`, resolve, reject, x))
}
