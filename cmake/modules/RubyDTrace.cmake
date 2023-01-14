function(add_ruby_dtrace_library target_name src)
  set(include_dirs "$<TARGET_PROPERTY:${target_name},INCLUDE_DIRECTORIES>")
  set(include_options "$<$<BOOL:${include_dirs}>:-I$<JOIN:${include_dirs}, -I>>")
  set(header ${CMAKE_CURRENT_BINARY_DIR}/probes.h)
  set(obj ${CMAKE_CURRENT_BINARY_DIR}/probes.o)
  add_custom_command(
    OUTPUT "${header}"
    COMMAND "${DTRACE}" -o "${header}.tmp" -h -C ${include_options} -s "${src}"
    COMMAND
      ${SED}
        "-e" "'s/RUBY_/RUBY_DTRACE_/g'"
        "-e" "'s/PROBES_H_TMP/RUBY_PROBES_H/'"
        "-e" "'s/(char \*/(const char */g'"
        "-e" "'s/, char \*/, const char */g'"
        "${header}.tmp" > "${header}"
    DEPENDS ${src}
  )
  set_source_files_properties("${header}" PROPERTIES HEADER_FILE_ONLY TRUE)

  add_custom_command(
    OUTPUT "${obj}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${obj}"
    COMMAND "${DTRACE}" -G -C ${include_options} -s "${src}" -o "${obj}"
  )
  add_library(${target_name} STATIC ${obj} ${header})
  set_target_properties(${target_name} PROPERTIES LINKER_LANGUAGE C)
endfunction()
