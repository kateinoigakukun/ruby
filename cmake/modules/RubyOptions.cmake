option(RUBY_ENABLE_YJIT
       "enable in-process JIT compiler that requires Rust build tools. enabled by default on supported platforms if rustc 1.58.0+ is available"
       FALSE)

set(RUBY_COROUTINE_default "amd64")
set(RUBY_COROUTINE
    "${RUBY_COROUTINE_default}" CACHE STRING
    "specify the coroutine implementation to use")

set(RUBY_ENABLE_DTRACE_default FALSE)
option(RUBY_ENABLE_DTRACE
       "enable DTrace for tracing inside ruby. enabled by default on systems having dtrace"
       ${RUBY_ENABLE_DTRACE_default})
