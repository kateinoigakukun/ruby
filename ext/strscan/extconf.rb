# frozen_string_literal: true
require 'mkmf'

unless respond_to?(:parallel_checks, true)
  # mkmf of Ruby 4.0 and earlier: run the checks one after another
  def parallel_checks(*) yield MakeMakefile end
end

if RUBY_ENGINE == 'ruby'
  $INCFLAGS << " -I$(top_srcdir)" if $extmk
  parallel_checks do |c|
    c.have_func("onig_region_memsize(NULL)")
    c.have_func("rb_reg_onig_match", "ruby/re.h")
    c.have_func("rb_deprecate_constant")
    c.have_func("rb_int_parse_cstr", "ruby.h") # RUBY_VERSION >= 2.5
    c.have_func("rb_gc_location", "ruby.h") # RUBY_VERSION >= 2.7
    c.have_const("RUBY_TYPED_EMBEDDABLE", "ruby.h") # RUBY_VERSION >= 3.3
  end
  create_makefile 'strscan'
else
  File.write('Makefile', dummy_makefile("").join)
end
