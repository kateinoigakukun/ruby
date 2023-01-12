function(ruby_generate_from_erb template)
  cmake_parse_arguments(RGFE "" "OUTPUT" "DEPENDS;OPTIONS;ARGS" ${ARGN})
  add_custom_command(
    OUTPUT "${RGFE_OUTPUT}"
    COMMAND
    ${BASERUBY} "${RUBY_TOOLDIR}/generic_erb.rb"
      ${RGFE_OPTIONS}
      "--output=${RGFE_OUTPUT}" "${template}"
      ${RGFE_ARGS}
    DEPENDS
      "${RUBY_TOOLDIR}/generic_erb.rb"
      "${template}"
      ${RGFE_DEPENDS}
  )
endfunction()

function(ruby_generate_rbinc rb_source output_out)
  # strip .rb extension from source file name
  get_filename_component(name ${rb_source} NAME_WE)
  set(output "${CMAKE_CURRENT_SOURCE_DIR}/${name}.rbinc")
  set(${output_out} "${output}" PARENT_SCOPE)
  add_custom_command(
    OUTPUT "${output}"
    COMMAND
      ${BASERUBY} "${RUBY_TOOLDIR}/mk_builtin_loader.rb" "${rb_source}"
    DEPENDS
      "${RUBY_TOOLDIR}/mk_builtin_loader.rb"
      "${rb_source}"
  )
  set_source_files_properties("${output}"
    PROPERTIES HEADER_FILE_ONLY TRUE)
endfunction()
