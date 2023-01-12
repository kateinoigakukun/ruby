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

