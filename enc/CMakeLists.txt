set(RUBY_BUILTIN_ENCS
  ascii.c
  us_ascii.c
  unicode.c
  utf_8.c)

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/trans/newline.c"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/trans"
  COMMAND "${BASERUBY}" "${RUBY_TOOLDIR}/transcode-tblgen.rb" -vo
    "${CMAKE_CURRENT_BINARY_DIR}/trans/newline.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/trans/newline.trans"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/trans/newline.trans"
    "${RUBY_TOOLDIR}/transcode-tblgen.rb"
)

add_library(rubyEncoding OBJECT
  ${RUBY_BUILTIN_ENCS}
  ${CMAKE_CURRENT_BINARY_DIR}/trans/newline.c
)
target_compile_definitions(rubyEncoding PRIVATE ${RUBY_COMPILE_DEFINITIONS})
target_include_directories(rubyEncoding PRIVATE ${RUBY_INCLUDE_DIRECTORIES})
