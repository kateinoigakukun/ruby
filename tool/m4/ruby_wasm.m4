dnl -*- Autoconf -*-
AC_DEFUN([RUBY_WASM],
[AS_CASE(["$target_os"],
[wasi*], [
    AC_CHECK_TOOL(WASMOPT, wasm-opt)
    AC_SUBST(wasmoptflags)
])
])dnl
