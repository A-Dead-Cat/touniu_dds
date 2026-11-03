// Minimal Emscripten entry point for the Touniu solver ABI. The module is a
// library (built with --no-entry); the exported C symbols are listed in
// EXPORTED_FUNCTIONS in BUILD.bazel. There is no UI or file-system logic here.

#include <touniu_solver/c_api.h>
