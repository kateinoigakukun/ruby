function(ruby_configure_coroutine type src_out header_out)
  set(header "coroutine/${type}/Context.h")

  if(NOT EXISTS ${header})
    message(FATAL_ERROR "'${type}' is not supported as coroutine")
  endif()

  set(src "coroutine/${type}/Context.c")
  if(NOT EXISTS ${src})
    set(src "coroutine/${type}/Context.S")
  endif()
  if(NOT EXISTS ${src})
    set(src "coroutine/${type}/Context.asm")
  endif()

  set(${src_out} ${src} PARENT_SCOPE)
  set(${header_out} ${header} PARENT_SCOPE)
endfunction()

