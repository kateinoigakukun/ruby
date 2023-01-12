function(ruby_configure_coroutine type src_out header_out)
  set(header "${CMAKE_CURRENT_SOURCE_DIR}/${type}/Context.h")

  if(NOT EXISTS ${header})
    message(FATAL_ERROR "'${type}' is not supported as coroutine")
  endif()

  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${type}/Context.c")
  if(NOT EXISTS ${src})
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/${type}/Context.S")
  endif()
  if(NOT EXISTS ${src})
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/${type}/Context.asm")
  endif()

  set(${src_out} ${src} PARENT_SCOPE)
  set(${header_out} ${header} PARENT_SCOPE)
endfunction()

ruby_configure_coroutine(${RUBY_COROUTINE}
  coroutine_src coroutine_header)

add_library(rubyCoroutine OBJECT ${coroutine_src})

target_compile_definitions(rubyCoroutine PRIVATE COROUTINE_H="${coroutine_header}")
