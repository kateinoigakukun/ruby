cmake_minimum_required(VERSION 3.17.0)

project(Ruby LANGUAGES C ASM)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

set(RUBY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(RUBY_TOOLDIR ${CMAKE_CURRENT_SOURCE_DIR}/tool)
set(RUBY_DOWNLOAD_CACHE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.downloaded-cache)

include(CheckTypeSize)
include(CheckCSourceCompiles)
include(CheckIncludeFiles)
include(CheckLinkerFlag)
include(CheckFunctionExists)
include(AutoconfMigration)
include(RubyOptions)
include(RubyBuildCodeGenerator)
include(RubyDTrace)
include(RubyFeatureCheck)

find_program(BISON bison)
find_program(DTRACE dtrace)
find_program(SED sed)
find_program(BASERUBY ruby)
if(NOT BASERUBY MATCHES "BASERUBY-NOTFOUND")
  set(BOOTSTRAPRUBY "${BASERUBY}")
else()
  # NOTE: Use of BOOTSTRAPRUBY in add_custom_command or add_custom_target
  # will add miniruby to its dependencies implicitly due to the generator-expression
  set(BOOTSTRAPRUBY "$<TARGET_FILE:miniruby>")
endif()
message(STATUS "BOOTSTRAPRUBY: ${BOOTSTRAPRUBY}")

ruby_check_headers()
ruby_check_types()
ruby_check_lang_features()
ruby_check_funcs(missing_srcs)

# TODO(katei): Generate config.h from config.h.in
# configure_file(./config.h.in ${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux/ruby/config.h)
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux/ruby/config.h")
  execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/configure" "-C" "--disable-jit-support"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/GNUmakefile"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
endif()

ac_load_cache_file()

# TODO(katei): guess platform symbol prefix
set(SYMBOL_PREFIX)

set(RUBY_INCLUDE_DIRECTORIES
  ${CMAKE_SOURCE_DIR}/
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/.ext/include/x86_64-linux
)
set(RUBY_COMPILE_DEFINITIONS RUBY_EXPORT)

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
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/vm_call_iseq_optimized.inc"
  PROPERTIES HEADER_FILE_ONLY TRUE)

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/known_errors.inc.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/known_errors.inc"
  OPTIONS "-c"
  ARGS ${CMAKE_CURRENT_SOURCE_DIR}/defs/known_errors.def
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/defs/known_errors.def)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/known_errors.inc"
  PROPERTIES HEADER_FILE_ONLY TRUE)

set(insns2vm_rb_outputs
  "${CMAKE_CURRENT_BINARY_DIR}/optinsn.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/optunifs.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/insns.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/insns_info.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/vmtc.inc"
  "${CMAKE_CURRENT_BINARY_DIR}/vm.inc")
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

ruby_generate_from_erb("${CMAKE_CURRENT_SOURCE_DIR}/template/prelude.c.tmpl"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/miniprelude.c"
  OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}"
  ARGS ${BUILTIN_RB_SOURCES}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/defs/known_errors.def)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/miniprelude.c"
  PROPERTIES HEADER_FILE_ONLY TRUE)

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

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lex.c"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/lex.c.blt" "${CMAKE_CURRENT_BINARY_DIR}/lex.c"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/defs/keywords"
)

set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/lex.c"
  PROPERTIES HEADER_FILE_ONLY TRUE)

add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/parse.c"
    "${CMAKE_CURRENT_BINARY_DIR}/parse.h"
  COMMAND ${BASERUBY} "${RUBY_TOOLDIR}/id2token.rb" "${CMAKE_CURRENT_SOURCE_DIR}/parse.y" > "${CMAKE_CURRENT_BINARY_DIR}/parse.tmp.y"
  COMMAND ${BISON} -d -o "${CMAKE_CURRENT_BINARY_DIR}/y.tab.c" "${CMAKE_CURRENT_BINARY_DIR}/parse.tmp.y"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/parse.tmp.y"
  COMMAND ${SED}
    -e "'/^#/s|parse\.tmp\.[iy]|${CMAKE_CURRENT_SOURCE_DIR}/parse.y|'"
    -e "'/^#/s!y\.tab\.c!${CMAKE_CURRENT_BINARY_DIR}/parse.c!'" y.tab.c > parse.c
  COMMAND ${SED} -e "'/^#line.*y\.tab\.h/d;/^#line.*parse.*\.y/d'" y.tab.h > "${CMAKE_CURRENT_BINARY_DIR}/parse.h"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/parse.y"
    "${RUBY_TOOLDIR}/ytab.sed"
    "${RUBY_TOOLDIR}/id2token.rb"
    "${CMAKE_CURRENT_BINARY_DIR}/id.h"
)

add_subdirectory(coroutine)
add_subdirectory(enc)

add_ruby_dtrace_library(rubyDTrace ${CMAKE_CURRENT_SOURCE_DIR}/probes.d)
target_include_directories(rubyDTrace PRIVATE ${RUBY_INCLUDE_DIRECTORIES})

add_library(ruby-static STATIC
  ${rbinc_outputs}
  ${CMAKE_CURRENT_BINARY_DIR}/insns.inc
  ${CMAKE_CURRENT_BINARY_DIR}/vm_call_iseq_optimized.inc
  ${CMAKE_CURRENT_BINARY_DIR}/id.h
  ${CMAKE_CURRENT_BINARY_DIR}/id.c
  ${CMAKE_CURRENT_BINARY_DIR}/known_errors.inc
  ${CMAKE_CURRENT_BINARY_DIR}/node_name.inc
  ${CMAKE_CURRENT_BINARY_DIR}/lex.c
  ${CMAKE_CURRENT_BINARY_DIR}/parse.c
  array.c
  ast.c
  bignum.c
  class.c
  compar.c
  compile.c
  complex.c
  cont.c
  debug.c
  debug_counter.c
  dir.c
  dln_find.c
  encoding.c
  enum.c
  enumerator.c
  error.c
  eval.c
  file.c
  gc.c
  hash.c
  inits.c
  io.c
  io_buffer.c
  iseq.c
  load.c
  marshal.c
  math.c
  memory_view.c
  rjit.c
  rjit_c.c
  node.c
  numeric.c
  object.c
  pack.c
  proc.c
  process.c
  ractor.c
  random.c
  range.c
  rational.c
  re.c
  regcomp.c
  regenc.c
  regerror.c
  regexec.c
  regparse.c
  regsyntax.c
  ruby.c
  scheduler.c
  shape.c
  signal.c
  sprintf.c
  st.c
  strftime.c
  string.c
  struct.c
  symbol.c
  thread.c
  time.c
  transcode.c
  util.c
  variable.c
  version.c
  vm.c
  vm_backtrace.c
  vm_dump.c
  vm_sync.c
  vm_trace.c
  $<IF:$<BOOL:${RUBY_ENABLE_YJIT}>,yjit.c,>
  ${missing_srcs}
)

target_include_directories(ruby-static
  PUBLIC ${RUBY_INCLUDE_DIRECTORIES})
target_compile_definitions(ruby-static PUBLIC ${RUBY_COMPILE_DEFINITIONS})
target_link_libraries(ruby-static PRIVATE rubyCoroutine rubyEncoding rubyDTrace)

add_executable(miniruby
  main.c
  ${CMAKE_CURRENT_BINARY_DIR}/miniprelude.c
  dmydln.c
  miniinit.c
  dmyext.c
  # TODO(katei): add conditional support
  addr2line.c
)
target_link_libraries(miniruby PRIVATE ruby-static)
# TODO(katei): Exlucde those dependency when unnecessary
target_link_libraries(miniruby PRIVATE m dl pthread rt z crypt)
