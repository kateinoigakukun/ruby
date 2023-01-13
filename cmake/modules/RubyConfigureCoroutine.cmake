function(ruby_configure_coroutine type src_out header_out)
  set(coroutine_dir "${RUBY_SOURCE_DIR}/coroutine/${type}")
  set(header "${coroutine_dir}/Context.h")

  if(NOT EXISTS ${header})
    message(FATAL_ERROR "'${type}' is not supported as coroutine because '${header}' does not exist")
  endif()

  set(src "${coroutine_dir}/Context.c")
  if(NOT EXISTS ${src})
    set(src "${coroutine_dir}/Context.S")
  endif()
  if(NOT EXISTS ${src})
    set(src "${coroutine_dir}/Context.asm")
  endif()

  set(${src_out} ${src} PARENT_SCOPE)
  set(${header_out} ${header} PARENT_SCOPE)
endfunction()

