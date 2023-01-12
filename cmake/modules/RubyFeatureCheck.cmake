function(ruby_check_func_exported out_var)
  set(RUBY_FUNC_EXPORTED)
  ruby_check_c_decl_attribute("__attribute__ ((__visibility__(\"default\"))) extern" RUBY_FUNC_EXPORTED___attribute___visibility_default)

  if(RUBY_FUNC_EXPORTED___attribute___visibility_default)
    set(${out_var} "__attribute__ ((__visibility__(\"default\"))) extern")
  else()
    ruby_check_c_decl_attribute("__declspec(dllexport) extern" RUBY_FUNC_EXPORTED___declspec_dllexport)
    if(RUBY_FUNC_EXPORTED___declspec_dllexport)
      set(${out_var} "__declspec(dllexport) extern")
    else()
      message(WARNING "${out_var} is not defined")
    endif()
  endif()
endfunction()

macro(ruby_check_headers)
  ac_header_stdbool()
  ac_check_headers(time.h)
  ac_check_headers(sys/time.h)
  ac_check_headers(string.h)
  ac_check_headers(stdlib.h)
  ac_check_headers(stdint.h)
endmacro()

macro(ruby_check_types)
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
endmacro()

macro(ruby_check_lang_features)
  # FIXME(katei): feature detection and macro/option definition should be separated?
  ruby_check_func_exported(RUBY_FUNC_EXPORTED)
  ruby_decl_attribute("__noreturn__" NORETURN)
  check_linker_flag(C "-fdeclspec" HAVE_FDECLSPEC_FLAG)
  if(HAVE_FDECLSPEC_FLAG)
    add_link_options("-fdeclspec")
    add_compile_options("-fdeclspec")
  endif()
endmacro()
