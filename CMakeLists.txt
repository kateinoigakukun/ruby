cmake_minimum_required(VERSION 3.17.0)

project(Ruby LANGUAGES C ASM)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

set(RUBY_TOOLDIR ${CMAKE_CURRENT_SOURCE_DIR}/tool)

include(CheckTypeSize)
include(CheckCSourceCompiles)
include(CheckIncludeFiles)
include(CheckLinkerFlag)
include(AutoconfMigration)

find_program(DTRACE dtrace)
find_program(SED sed)
find_program(BASERUBY ruby)

ac_header_stdbool()
ac_check_headers(time.h)
ac_check_headers(sys/time.h)

set(saved_CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS})
set(CMAKE_REQUIRED_DEFINITIONS)
if(HAVE_TIME_H)
  list(APPEND CMAKE_REQUIRED_DEFINITIONS -DHAVE_TIME_H)
endif()
if(HAVE_SYS_TIME_H)
  list(APPEND CMAKE_REQUIRED_DEFINITIONS -DHAVE_SYS_TIME_H)
endif()
set(TIME_H_SYS_TIME_H_PRELUDE
  "#if HAVE_TIME_H
  # include <time.h>
  #endif
  #if HAVE_SYS_TIME_H
  # include <sys/time.h>
  #endif")

ac_check_types("struct timeval" "${TIME_H_SYS_TIME_H_PRELUDE}")
ac_check_types("struct timespec" "${TIME_H_SYS_TIME_H_PRELUDE}")
ac_check_types("struct timezone" "${TIME_H_SYS_TIME_H_PRELUDE}")
set(CMAKE_REQUIRED_DEFINITIONS ${saved_CMAKE_REQUIRED_DEFINITIONS})


set(RUBY_FUNC_EXPORTED)
ruby_check_c_decl_attribute("__attribute__ ((__visibility__(\"default\"))) extern" RUBY_FUNC_EXPORTED___attribute___visibility_default)

if(RUBY_FUNC_EXPORTED___attribute___visibility_default)
  set(RUBY_FUNC_EXPORTED "__attribute__ ((__visibility__(\"default\"))) extern")
else()
  ruby_check_c_decl_attribute("__declspec(dllexport) extern" RUBY_FUNC_EXPORTED___declspec_dllexport)
  if(RUBY_FUNC_EXPORTED___declspec_dllexport)
    set(RUBY_FUNC_EXPORTED "__declspec(dllexport) extern")
  else()
    message(WARNING "RUBY_FUNC_EXPORTED is not defined")
  endif()
endif()

ruby_decl_attribute("__noreturn__" NORETURN)

check_linker_flag(C "-fdeclspec" HAVE_FDECLSPEC_FLAG)
if(HAVE_FDECLSPEC_FLAG)
  add_link_options("-fdeclspec")
  add_compile_options("-fdeclspec")
endif()

# TODO(katei): Generate config.h from config.h.in
# configure_file(./config.h.in ${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux/ruby/config.h)
configure_file(./config.h ${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux/ruby/config.h)

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

function(ruby_generate_from_erb template)
  cmake_parse_arguments(RGFE "" "OUTPUT" "DEPENDS" ${ARGN})
  add_custom_command(
    OUTPUT "${RGFE_OUTPUT}"
    COMMAND
    ${BASERUBY} "${RUBY_TOOLDIR}/generic_erb.rb"
      "--output=${RGFE_OUTPUT}" "${template}"
    DEPENDS
      "${RUBY_TOOLDIR}/generic_erb.rb"
      "${template}"
      ${RGFE_DEPENDS}
  )
endfunction()

set(RUBY_INCLUDE_DIRECTORIES
  .
  ./include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux)

add_ruby_dtrace_header(probes_h ${CMAKE_CURRENT_SOURCE_DIR}/probes.d probes.h)
set_target_properties(probes_h PROPERTIES
  INCLUDE_DIRECTORIES "${RUBY_INCLUDE_DIRECTORIES}")

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/id.h.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/id.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/defs/id.def")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/id.h"
  PROPERTIES HEADER_FILE_ONLY true)

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/id.c.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/id.c"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/defs/id.def")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/id.c"
  PROPERTIES HEADER_FILE_ONLY true)

add_library(ruby-static STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/id.h
  ${CMAKE_CURRENT_BINARY_DIR}/id.c
  ${CMAKE_CURRENT_BINARY_DIR}/probes.h
  ruby.c)
target_include_directories(ruby-static
  PUBLIC ${RUBY_INCLUDE_DIRECTORIES})
