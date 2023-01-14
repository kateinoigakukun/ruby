include(RubyConfigureCoroutine)

ruby_configure_coroutine(${RUBY_COROUTINE} coroutine_src coroutine_header)
add_library(rubyCoroutine OBJECT ${coroutine_src})
target_compile_definitions(rubyCoroutine PRIVATE "SYMBOL_PREFIX=${SYMBOL_PREFIX}")
