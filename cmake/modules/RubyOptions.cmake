option(RUBY_ENABLE_YJIT
       "enable in-process JIT compiler that requires Rust build tools. enabled by default on supported platforms if rustc 1.58.0+ is available"
       FALSE)

set(RUBY_COROUTINE_default "amd64")
set(RUBY_COROUTINE
    "${RUBY_COROUTINE_default}" CACHE STRING
    "specify the coroutine implementation to use")
