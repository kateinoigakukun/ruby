# frozen_string_literal: false
require_relative 'base'

class TestMkmfPch < TestMkmf
  def setup
    super
    unless CONFIG['GCC'] == 'yes' and !$universal
      omit "the prelude is precompiled only for a GCC compatible compiler"
    end
  end

  def uses_pch?
    MKMFLOG[].lines.grep(/conftest\.c|-o conftest\b/).last.include?(" -include mkmf-pch-")
  end

  def test_compile_and_link
    assert_equal(true, try_compile(""), MKMFLOG)
    assert_predicate(self, :uses_pch?, MKMFLOG)
    header = MKMFLOG[][/ -include (mkmf-pch-\d+\.h)/, 1]
    assert_file.exist?(header)
    assert_file.exist?("#{header}.gch")
    assert_file.not_exist?("conftest.c")
    assert_equal(true, try_link("int main(void) {return 0;}"), MKMFLOG)
    assert_predicate(self, :uses_pch?, MKMFLOG)
    conftest_pch_clean
    assert_file.not_exist?(header)
    assert_file.not_exist?("#{header}.gch")
  end

  def test_not_with_options
    assert_equal(true, try_compile("", "-DFOO=1"), MKMFLOG)
    assert_not_predicate(self, :uses_pch?, MKMFLOG)
    assert_equal(true, try_link("int main(void) {return 0;}", ldflags: "-L."), MKMFLOG)
    assert_not_predicate(self, :uses_pch?, MKMFLOG)
  end

  def test_not_with_changed_prelude
    assert_equal(true, try_compile("") {|src| "#define FOO 1\n" + src}, MKMFLOG)
    assert_not_predicate(self, :uses_pch?, MKMFLOG)
  end

  def test_not_for_preprocessor
    assert_equal(true, try_cpp(""), MKMFLOG)
    assert_not_include(MKMFLOG[], " -include mkmf-pch-")
  end
end
