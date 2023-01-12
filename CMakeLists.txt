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
  PROPERTIES HEADER_FILE_ONLY TRUE)

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/id.c.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/id.c"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/defs/id.def")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/id.c"
  PROPERTIES HEADER_FILE_ONLY TRUE)

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/call_iseq_optimized.inc.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vm_call_iseq_optimized.inc")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/vm_call_iseq_optimized.c"
  PROPERTIES HEADER_FILE_ONLY TRUE)

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/known_errors.inc.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/known_errors.inc"
  OPTIONS "-c"
  ARGS ${CMAKE_CURRENT_SOURCE_DIR}/defs/known_errors.def
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/defs/known_errors.def)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/known_errors.inc"
  PROPERTIES HEADER_FILE_ONLY TRUE)

set(insns2vm_rb_outputs
  "${CMAKE_CURRENT_BINARY_DIR}/opt_sc.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/optinsn.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/optunifs.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/insns.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/insns_info.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/vmtc.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/vm.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/mjit_sp_inc.inc")
add_custom_command(
  OUTPUT ${insns2vm_rb_outputs}
  COMMAND
    ${BASERUBY} -Ku ${RUBY_TOOLDIR}/insns2vm.rb ${INSNS2VMOPT} ${insns2vm_rb_outputs}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/insns.def
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_opts.h
    ${CMAKE_CURRENT_SOURCE_DIR}/defs/opt_operand.def
    ${CMAKE_CURRENT_SOURCE_DIR}/defs/opt_insn_unif.def
    ${RUBY_TOOLDIR}/insns2vm.rb
    ${RUBY_TOOLDIR}/ruby_vm/controllers/application_controller.rb
    ${RUBY_TOOLDIR}/ruby_vm/helpers/c_escape.rb
    ${RUBY_TOOLDIR}/ruby_vm/helpers/dumper.rb
    ${RUBY_TOOLDIR}/ruby_vm/helpers/scanner.rb
    ${RUBY_TOOLDIR}/ruby_vm/loaders/insns_def.rb
    ${RUBY_TOOLDIR}/ruby_vm/loaders/opt_insn_unif_def.rb
    ${RUBY_TOOLDIR}/ruby_vm/loaders/opt_operand_def.rb
    ${RUBY_TOOLDIR}/ruby_vm/loaders/vm_opts_h.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/attribute.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/bare_instructions.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/c_expr.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/instructions.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/instructions_unifications.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/operands_unifications.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/trace_instructions.rb
    ${RUBY_TOOLDIR}/ruby_vm/models/typemap.rb
    ${RUBY_TOOLDIR}/ruby_vm/scripts/converter.rb
    ${RUBY_TOOLDIR}/ruby_vm/scripts/insns2vm.rb
    ${RUBY_TOOLDIR}/ruby_vm/views/_attributes.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_c_expr.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_comptime_insn_stack_increase.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_copyright.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_insn_entry.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_insn_len_info.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_insn_name_info.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_insn_operand_info.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_insn_sp_pc_dependency.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_insn_type_chars.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_leaf_helpers.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_notice.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_sp_inc_helpers.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/_trace_instruction.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/insns.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/insns_info.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/mjit_sp_inc.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/opt_sc.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/optinsn.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/optunifs.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/vm.inc.erb
    ${RUBY_TOOLDIR}/ruby_vm/views/vmtc.inc.erb
)
foreach(output ${insns2vm_rb_outputs})
  set_source_files_properties("${output}"
    PROPERTIES HEADER_FILE_ONLY TRUE)
endforeach()

set(BUILTIN_RB_SOURCES
  array.rb
  ast.rb
  dir.rb
  gc.rb
  gem_prelude.rb
  io.rb
  kernel.rb
  marshal.rb
  mjit.rb
  mjit_c.rb
  nilclass.rb
  numeric.rb
  pack.rb
  prelude.rb
  ractor.rb
  symbol.rb
  thread_sync.rb
  timev.rb
  trace_point.rb
  warning.rb
  yjit.rb)
set(rbinc_outputs)
foreach(source ${BUILTIN_RB_SOURCES})
  ruby_generate_rbinc(${CMAKE_SOURCE_DIR}/${source} rbinc_output)
  list(APPEND rbinc_outputs ${rbinc_output})
endforeach()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_name.inc
  COMMAND
    ${BASERUBY} -n ${RUBY_TOOLDIR}/node_name.rb
      < ${CMAKE_CURRENT_SOURCE_DIR}/node.h > ${CMAKE_CURRENT_BINARY_DIR}/node_name.inc
  DEPENDS
    ${RUBY_TOOLDIR}/node_name.rb
    ${CMAKE_CURRENT_SOURCE_DIR}/node.h
)

set(ARCH_FLAG)
set(UNIVERSAL_ARCHNAMES)

add_library(ruby-static STATIC
  ${rbinc_outputs}
  ${CMAKE_CURRENT_BINARY_DIR}/insns.inc
  ${CMAKE_CURRENT_BINARY_DIR}/vm_call_iseq_optimized.inc
  ${CMAKE_CURRENT_BINARY_DIR}/id.h
  ${CMAKE_CURRENT_BINARY_DIR}/id.c
  ${CMAKE_CURRENT_BINARY_DIR}/probes.h
  ${CMAKE_CURRENT_BINARY_DIR}/known_errors.inc
  ${CMAKE_CURRENT_BINARY_DIR}/node_name.inc
  vm.c
  ruby.c)

target_include_directories(ruby-static
  PUBLIC ${RUBY_INCLUDE_DIRECTORIES})
