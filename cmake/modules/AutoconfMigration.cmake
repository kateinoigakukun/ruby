function(ac_check_types type_list prelude)
  foreach(typename ${type_list})
    string(TOUPPER "HAVE_${typename}" varname)
    string(REPLACE " " "_" varname "${varname}")
    string(REPLACE "*" "p" varname "${varname}")
    check_c_source_compiles(
      "${prelude}
      ${typename} conftest_types;
      int main(void) { return 0; }"
      ${varname})
  endforeach()
endfunction()

macro(ac_check_headers)
  foreach(header ${ARGN})
    string(TOUPPER HAVE_${header} var)
    string(REGEX REPLACE "\\.|/" "_" var ${var})
    if(NOT DEFINED ${var})
      check_include_files(${header} ${var})
    endif()
  endforeach()
endmacro()

macro(ac_header_stdbool)
  ac_check_headers(stdbool.h)
  check_type_size(_Bool HAVE__BOOL)
endmacro()

macro(ac_load_cache_file)
  # Read config.cache and extract all entries
  message(STATUS "Loading config.cache:")
  file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/config.cache config_cache)
  foreach(line ${config_cache})
    # Parse autoconf cache entry: e.g. ac_cv_build=${ac_cv_build=x86_64-pc-linux-gnu}
    if(line MATCHES "^(.+)=\\$\\{(.+)\\}$")
      set(var ${CMAKE_MATCH_1})
      set(value ${CMAKE_MATCH_2})
      if(value MATCHES "^${var}=(.*)$")
        set(value ${CMAKE_MATCH_1})
      endif()
      set(${var} ${value} CACHE INTERNAL "")
      message(STATUS "  ${var}=${value}")
    endif()
  endforeach()
endmacro()

macro(ruby_check_c_decl_attribute _ATTRIBUTE _RESULT)
  check_c_source_compiles("${_ATTRIBUTE} int conftest_func() { return 0; }
    int main() { return conftest_func(); }" ${_RESULT}
  )
endmacro()

function(ruby_decl_attribute attrib macroname)
  if(${macroname})
    return()
  else()
    message(STATUS "Performing Test ${macroname}")

    set(saved_CMAKE_REQUIRED_QUIET ${CMAKE_REQUIRED_QUIET})
    set(CMAKE_REQUIRED_QUIET ON)

    set(candiates
      "__attribute__ ((${attrib})) x"
      "x __attribute__ ((${attrib}))"
      "__declspec(${attrib}) x;x")
    set(found OFF)

    foreach(candidate ${candiates})
      set(macro_definition "#define ${macroname}(x) ${candidate}")
      check_c_source_compiles(
        "${macro_definition}
        #define x int conftest_attribute_check(void)
        ${macroname}(x);
        int main() { return 0; }" tmp_cachevar
      )
      set(has_attribute ${tmp_cachevar})
      unset(tmp_cachevar CACHE)
      if(has_attribute)
        set(${macroname} "${macro_definition}" CACHE INTERNAL "")
        set(found ON)
        break()
      endif()
    endforeach()

    if(found)
      message(STATUS "Performing Test ${macroname} - Success")
    else()
      message(STATUS "Performing Test ${macroname} - Failed")
    endif()

    set(CMAKE_REQUIRED_QUIET ${saved_CMAKE_REQUIRED_QUIET})
  endif()
endfunction()

macro(ruby_replace_func funcname missing_srcs_out)
  string(TOUPPER "HAVE_${funcname}" varname)
  string(REPLACE " " "_" varname "${varname}")
  check_function_exists("${funcname}" "${varname}")
  if(NOT "${${varname}}")
    list(APPEND "${missing_srcs_out}" "${RUBY_SOURCE_DIR}/missing/${funcname}.c")
  endif()
endmacro()
