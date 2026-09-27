# frozen_string_literal: true
require 'mkmf'

unless respond_to?(:parallel_checks, true)
  # mkmf of Ruby 4.0 and earlier: run the checks one after another
  def parallel_checks(*) yield MakeMakefile end
end

$defs << "-DJSON_DEBUG" if ENV.fetch("JSON_DEBUG", "0") != "0"

if RUBY_ENGINE == 'truffleruby' && RUBY_ENGINE_VERSION < '40.0'
  # Ref: https://github.com/truffleruby/truffleruby/issues/4329
  # Ref: https://github.com/truffleruby/truffleruby/pull/4333
  $defs << "-DJSON_TRUFFLERUBY_RB_CATCH_BUG"
end

parallel_checks do |c|
  c.have_func("rb_enc_interned_str", "ruby/encoding.h") # RUBY_VERSION >= 3.0
  c.have_func("rb_str_to_interned_str", "ruby.h") # RUBY_VERSION >= 3.0
  c.have_func("rb_hash_new_capa", "ruby.h") # RUBY_VERSION >= 3.2
  c.have_func("rb_hash_bulk_insert", "ruby.h") # Missing on TruffleRuby
  c.have_func("ruby_xfree_sized", "ruby.h") # RUBY_VERSION >= 4.1

  c.then do
    if have_header("x86intrin.h")
      have_func("_lzcnt_u64", "x86intrin.h")
    end
  end

  c.then do
    if have_header("intrin.h")
      have_func("__lzcnt64", "intrin.h")
      have_func("_BitScanReverse64", "intrin.h")
    end
  end

  if RUBY_ENGINE == "ruby"
    c.have_const("RUBY_TYPED_EMBEDDABLE", "ruby.h") # RUBY_VERSION >= 3.3
  end
end

append_cflags("-std=c99")

# Disable function outlining on clang to prevent some of the repeated instructions
# in json_eat_whitespace being outlined into function calls.
# Note: This verifies '-mno-outline' is accepted as a valid compiler flag
# and will not pass it if unsupported.
append_cflags("-mno-outline")

if enable_config('parser-use-simd', default=!ENV["JSON_DISABLE_SIMD"])
  load __dir__ + "/../simd/conf.rb"
end

create_makefile 'json/ext/parser'
