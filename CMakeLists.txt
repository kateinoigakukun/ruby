cmake_minimum_required(VERSION 3.17.0)

project(Ruby LANGUAGES C ASM)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

include(CheckTypeSize)
include(CheckCSourceCompiles)
include(CheckIncludeFiles)
include(CheckLinkerFlag)
include(AutoconfMigration)

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

set(RUBY_INCLUDE_DIRECTORIES
  .
  ./include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux)
add_library(ruby-static STATIC
  ruby.c)
target_include_directories(ruby-static
  PUBLIC ${RUBY_INCLUDE_DIRECTORIES})
