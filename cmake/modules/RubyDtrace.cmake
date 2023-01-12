function(add_ruby_dtrace_header target_name src header)
  set(header_dirs "$<$<BOOL:${prop}>:-I$<JOIN:${prop},;-I>>")
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${header}
    COMMAND
      ${DTRACE} -o ${header}.tmp -h -C
        "$<$<BOOL:${header_dirs}>:-I$<JOIN:${header_dirs},;-I>>" -s ${src}
    COMMAND
      ${SED}
        "-e" "'s/RUBY_/RUBY_DTRACE_/g'"
        "-e" "'s/PROBES_H_TMP/RUBY_PROBES_H/'"
        "-e" "'s/(char \*/(const char */g'"
        "-e" "'s/, char \*/, const char */g'"
        "${header}.tmp" > probes.h
    DEPENDS ${src}
  )
  add_custom_target(${target_name} DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${header})
endfunction()

