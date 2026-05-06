#include "r_api.h"
#include <R_ext/Visibility.h>
#include <R_ext/Rdynload.h>
#include <cstdint>
#include <string>
#include <sstream>
#ifndef _WIN32
#include <poll.h>
#else
#include <winsock2.h>
#endif

// Forward declarations for all C++ functions defined in the other src/ files.
// These have C++ linkage (not extern "C") so that C++ name-mangling rules apply.

// callback_registry.cpp
void testCallbackOrdering();

// debug.cpp
std::string log_level(std::string level);
bool using_ubsan();

// fd.cpp
SEXP execLater_fd(SEXP callback, SEXP readfds, SEXP writefds, SEXP exceptfds,
                  SEXP timeoutSecs, SEXP loop_id);
SEXP fd_cancel(SEXP xptr);

// later.cpp
void setCurrentRegistryId(int id);
int getCurrentRegistryId();
bool deleteCallbackRegistry(int loop_id);
bool notifyRRefDeleted(int loop_id);
void createCallbackRegistry(int id, int parent_id);
bool existsCallbackRegistry(int id);
SEXP list_queue_(int id);
bool execCallbacks(double timeoutSecs, bool runAll, int loop_id);
bool idle(int loop_id);
void ensureInitialized();
std::string execLater(SEXP callback, double delaySecs, int loop_id);
bool cancel(std::string callback_id_s, int loop_id);
double nextOpSecs(int loop_id);

// wref.c
extern "C" SEXP _later_new_weakref(SEXP);
extern "C" SEXP _later_wref_key(SEXP);

// test_api.cpp
int later_dll_api_version();
int later_h_api_version();
int testfd();
void launchBgTask(int secsToSleep);
void checkLaterOrdering();
void cpp_error(int value);
void asyncFib(SEXP resolve, SEXP reject, double x);

// ============================================================================
// Helper macro for uniform exception handling in every wrapper.
// On R longjmp: continue the R unwind.
// On std::exception: call Rf_error (which longjmps to R).
// On unknown: call Rf_error with a generic message.
// ============================================================================

#define BEGIN_RAPI_WRAPPER \
  try {

#define END_RAPI_WRAPPER \
  } catch (unwind_exception& e) { \
    R_ContinueUnwind(e.token); \
  } catch (std::exception& e) { \
    Rf_error("%s", e.what()); \
  } catch (...) { \
    Rf_error("C++ exception (unknown reason)"); \
  } \
  return R_NilValue; /* unreachable, satisfies compiler */

// ============================================================================
// Wrappers
// ============================================================================

extern "C" SEXP _later_testCallbackOrdering() {
  BEGIN_RAPI_WRAPPER
    testCallbackOrdering();
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_log_level(SEXP level) {
  BEGIN_RAPI_WRAPPER
    std::string lvl(CHAR(STRING_ELT(level, 0)));
    std::string result = log_level(lvl);
    return Rf_mkString(result.c_str());
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_using_ubsan() {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarLogical(using_ubsan() ? TRUE : FALSE);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_execLater_fd(SEXP callback, SEXP readfds, SEXP writefds,
                                     SEXP exceptfds, SEXP timeoutSecs, SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    return execLater_fd(callback, readfds, writefds, exceptfds, timeoutSecs, loop_id);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_fd_cancel(SEXP xptr) {
  BEGIN_RAPI_WRAPPER
    return fd_cancel(xptr);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_setCurrentRegistryId(SEXP id) {
  BEGIN_RAPI_WRAPPER
    setCurrentRegistryId(INTEGER(id)[0]);
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_getCurrentRegistryId() {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarInteger(getCurrentRegistryId());
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_deleteCallbackRegistry(SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarLogical(deleteCallbackRegistry(INTEGER(loop_id)[0]) ? TRUE : FALSE);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_notifyRRefDeleted(SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarLogical(notifyRRefDeleted(INTEGER(loop_id)[0]) ? TRUE : FALSE);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_createCallbackRegistry(SEXP id, SEXP parent_id) {
  BEGIN_RAPI_WRAPPER
    createCallbackRegistry(INTEGER(id)[0], INTEGER(parent_id)[0]);
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_existsCallbackRegistry(SEXP id) {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarLogical(existsCallbackRegistry(INTEGER(id)[0]) ? TRUE : FALSE);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_list_queue_(SEXP id) {
  BEGIN_RAPI_WRAPPER
    return list_queue_(INTEGER(id)[0]);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_execCallbacks(SEXP timeoutSecs, SEXP runAll, SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarLogical(
      execCallbacks(Rf_asReal(timeoutSecs), LOGICAL(runAll)[0] != FALSE, INTEGER(loop_id)[0])
      ? TRUE : FALSE
    );
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_idle(SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarLogical(idle(INTEGER(loop_id)[0]) ? TRUE : FALSE);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_ensureInitialized() {
  BEGIN_RAPI_WRAPPER
    ensureInitialized();
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_execLater(SEXP callback, SEXP delaySecs, SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    std::string id = execLater(callback, Rf_asReal(delaySecs), INTEGER(loop_id)[0]);
    return Rf_mkString(id.c_str());
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_cancel(SEXP callback_id_s, SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    std::string id_s(CHAR(STRING_ELT(callback_id_s, 0)));
    return Rf_ScalarLogical(cancel(id_s, INTEGER(loop_id)[0]) ? TRUE : FALSE);
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_nextOpSecs(SEXP loop_id) {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarReal(nextOpSecs(INTEGER(loop_id)[0]));
  END_RAPI_WRAPPER
}

// ============================================================================
// Test API wrappers (previously in latertest/)
// ============================================================================

extern "C" SEXP _later_later_dll_api_version() {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarInteger(later_dll_api_version());
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_later_h_api_version() {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarInteger(later_h_api_version());
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_testfd() {
  BEGIN_RAPI_WRAPPER
    return Rf_ScalarInteger(testfd());
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_launchBgTask(SEXP secsToSleep) {
  BEGIN_RAPI_WRAPPER
    launchBgTask(Rf_asInteger(secsToSleep));
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_checkLaterOrdering() {
  BEGIN_RAPI_WRAPPER
    checkLaterOrdering();
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_cpp_error(SEXP value) {
  BEGIN_RAPI_WRAPPER
    cpp_error(Rf_asInteger(value));
    return R_NilValue;
  END_RAPI_WRAPPER
}

extern "C" SEXP _later_asyncFib(SEXP resolve, SEXP reject, SEXP x) {
  BEGIN_RAPI_WRAPPER
    asyncFib(resolve, reject, Rf_asReal(x));
    return R_NilValue;
  END_RAPI_WRAPPER
}

// ============================================================================
// Registration
// ============================================================================

extern "C" {

static const R_CallMethodDef CallEntries[] = {
  // Core API
  {"_later_cancel",                   (DL_FUNC) &_later_cancel,                   2},
  {"_later_createCallbackRegistry",   (DL_FUNC) &_later_createCallbackRegistry,   2},
  {"_later_deleteCallbackRegistry",   (DL_FUNC) &_later_deleteCallbackRegistry,   1},
  {"_later_ensureInitialized",        (DL_FUNC) &_later_ensureInitialized,        0},
  {"_later_execCallbacks",            (DL_FUNC) &_later_execCallbacks,            3},
  {"_later_execLater",                (DL_FUNC) &_later_execLater,                3},
  {"_later_execLater_fd",             (DL_FUNC) &_later_execLater_fd,             6},
  {"_later_existsCallbackRegistry",   (DL_FUNC) &_later_existsCallbackRegistry,   1},
  {"_later_fd_cancel",                (DL_FUNC) &_later_fd_cancel,                1},
  {"_later_getCurrentRegistryId",     (DL_FUNC) &_later_getCurrentRegistryId,     0},
  {"_later_idle",                     (DL_FUNC) &_later_idle,                     1},
  {"_later_list_queue_",              (DL_FUNC) &_later_list_queue_,              1},
  {"_later_log_level",                (DL_FUNC) &_later_log_level,                1},
  {"_later_new_weakref",              (DL_FUNC) &_later_new_weakref,              1},
  {"_later_nextOpSecs",               (DL_FUNC) &_later_nextOpSecs,               1},
  {"_later_notifyRRefDeleted",        (DL_FUNC) &_later_notifyRRefDeleted,        1},
  {"_later_setCurrentRegistryId",     (DL_FUNC) &_later_setCurrentRegistryId,     1},
  {"_later_testCallbackOrdering",     (DL_FUNC) &_later_testCallbackOrdering,     0},
  {"_later_using_ubsan",              (DL_FUNC) &_later_using_ubsan,              0},
  {"_later_wref_key",                 (DL_FUNC) &_later_wref_key,                 1},
  // Test API (previously in latertest/)
  {"_later_later_dll_api_version",    (DL_FUNC) &_later_later_dll_api_version,    0},
  {"_later_later_h_api_version",      (DL_FUNC) &_later_later_h_api_version,      0},
  {"_later_testfd",                   (DL_FUNC) &_later_testfd,                   0},
  {"_later_launchBgTask",             (DL_FUNC) &_later_launchBgTask,             1},
  {"_later_checkLaterOrdering",       (DL_FUNC) &_later_checkLaterOrdering,       0},
  {"_later_cpp_error",                (DL_FUNC) &_later_cpp_error,                1},
  {"_later_asyncFib",                 (DL_FUNC) &_later_asyncFib,                 3},
  {NULL, NULL, 0}
};

} // extern "C"

// Forward declarations for C callables exported via R_RegisterCCallable.
extern "C" uint64_t execLaterNative2(void (*func)(void*), void* data, double delaySecs, int loop_id);
extern "C" int execLaterFdNative(void (*func)(int *, void *), void *data, int num_fds, struct pollfd *fds, double timeoutSecs, int loop_id);
extern "C" int apiVersion();

extern "C" attribute_visible void R_init_later(DllInfo* dll) {
  R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
  R_useDynamicSymbols(dll, FALSE);
  R_forceSymbols(dll, TRUE);

  // Register C callables for other packages (e.g. httpuv, promises) to use via
  // R_GetCCallable("later", "...").
  R_RegisterCCallable("later", "execLaterNative2",  (DL_FUNC) execLaterNative2);
  R_RegisterCCallable("later", "execLaterFdNative", (DL_FUNC) execLaterFdNative);
  R_RegisterCCallable("later", "apiVersion",        (DL_FUNC) apiVersion);
}
