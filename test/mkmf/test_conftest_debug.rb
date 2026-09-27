# frozen_string_literal: false
require_relative 'base'

class TestMkmfConftestDebug < TestMkmf
  def setup
    super
    unless CONFIG['GCC'] == 'yes'
      omit "the debugging flags are left out only for a GCC compatible compiler"
    end
    @debugflags = RbConfig::CONFIG['debugflags']
    if !@debugflags or @debugflags.strip.empty?
      omit "no debugging flags are configured"
    end
  end

  def last_command
    MKMFLOG[].lines.grep(/conftest\.c/).last
  end

  def assert_debugflags(included, msg = MKMFLOG)
    flags = @debugflags.split
    if included
      flags.each {|f| assert_match(/\s#{Regexp.quote(f)}\s/, last_command, msg)}
    else
      flags.each {|f| assert_not_match(/\s#{Regexp.quote(f)}\s/, last_command, msg)}
    end
  end

  def test_plain_checks
    assert_equal(true, try_compile(""), MKMFLOG)
    assert_debugflags(false)
    assert_equal(true, try_link("int main(void) {return 0;}"), MKMFLOG)
    assert_debugflags(false)
    if MakeMakefile::CONFTEST_STRIP_LDFLAGS
      assert_include(last_command, " #{MakeMakefile::CONFTEST_STRIP_LDFLAGS} ")
    end
  end

  def test_with_options
    assert_equal(true, try_compile("", "-DFOO=1"), MKMFLOG)
    assert_debugflags(true)
    assert_equal(true, try_cflags("-g"), MKMFLOG)
    assert_debugflags(true)
    assert_equal(true, try_link("int main(void) {return 0;}", ldflags: "-L."), MKMFLOG)
    assert_debugflags(true)
    assert_not_include(last_command, " -Wl,-S ")
  end

  def test_preprocessor
    assert_equal(true, try_cpp(""), MKMFLOG)
    assert_debugflags(true)
  end

  def test_config_untouched
    try_compile("")
    assert_equal(@debugflags, RbConfig::CONFIG['debugflags'])
    assert_include(RbConfig::CONFIG['cflags'], @debugflags)
  end
end
