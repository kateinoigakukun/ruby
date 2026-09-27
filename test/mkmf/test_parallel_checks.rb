# frozen_string_literal: false
require_relative 'base'

class TestMkmfParallelChecks < TestMkmf
  def setup
    super
    omit "parallel checks are not supported" unless MakeMakefile::ParallelChecks.supported?
  end

  def checks(c)
    c.have_func("ruby_init")
    c.have_func("no_ruby_init")
    c.have_header("ruby/ruby.h")
    c.have_header("no/such/header.h")
    c.have_struct_member("struct timeval", "tv_sec", "sys/time.h")
    c.have_struct_member("struct timeval", "no_member", "sys/time.h")
    c.have_type("size_t")
    c.have_type("struct no_such_type")
    c.have_const("EINVAL", "errno.h")
    c.have_const("NO_SUCH_CONST")
    c.have_macro("RUBY_API_VERSION_MAJOR", "ruby/version.h")
    c.have_var("errno", "errno.h")
    c.have_type("int") {|src| src + "#error forced\n"}
    c.try_compile("int x(void){int y; return 0;}", "-Wall", werror: true)
    c.try_compile("int x(void){return 0;}", "", werror: true)
    c.check_sizeof("int")
    c.egrep_cpp(/EINVAL/, "#include <errno.h>\n")
    c.egrep_cpp("EINVAL", "#include <errno.h>\n")
    c.try_run("int main(void){return 0;}")
    c.then {have_func("no_ruby_init") or have_func("ruby_init")}
    c.then {$defs << "-DEXTRA"; have_header("no/such/header.h")}
  end

  def conftest_files
    Dir.children(".").grep(/\Aconftest|\Amkmf\d+conf/)
  end

  def run_checks(jobs)
    have_devel?
    Logging.log_close
    FileUtils.rm_f("mkmf.log")
    $defs = []
    checks = method(:checks)
    results = mkmf {parallel_checks(jobs) {|c| checks.(c)}}
    Logging.log_close
    log = MKMFLOG[].gsub(%r[/cc\w+\.o\b], "/ccXXXXXX.o")
    [results, $defs, log, conftest_files]
  end

  def test_same_as_sequential
    sequential = run_checks(1)
    parallel = run_checks(4)
    assert_equal(sequential[0], parallel[0], "results")
    assert_equal(sequential[1], parallel[1], "$defs")
    assert_equal(sequential[2], parallel[2], "mkmf.log")
    assert_empty(parallel[3], "files left")
  end

  def test_build_configuration_changed
    [1, 4].each do |jobs|
      $defs = []
      cflags = $CFLAGS.dup
      e = assert_raise(RuntimeError) do
        mkmf {parallel_checks(jobs) {|c|
            c.have_func("ruby_init")
            c.checking_for("changing $CFLAGS") {$CFLAGS += " -DFOO"}
            c.have_func("ruby_init")
          }}
      end
      assert_match(/build configuration changed/, e.message)
      assert_equal(["-DHAVE_RUBY_INIT"], $defs)
      assert_empty(conftest_files)
    ensure
      $CFLAGS = cflags
    end
  end

  def test_jobs
    makeflags, makelevel, mflags = ENV["MAKEFLAGS"], ENV["MAKELEVEL"], $mflags
    ENV["MAKEFLAGS"], ENV["MAKELEVEL"] = " -j3 --jobserver-auth=3,4", "1"
    assert_equal(3, parallel_checks_jobs)
    ENV["MAKEFLAGS"] = "k"
    assert_equal(1, parallel_checks_jobs)
    default = parallel_checks_default_jobs
    assert_operator(default, :>=, 1)
    assert_operator(default, :<=, 8)
    ENV["MAKEFLAGS"] = "k -j"
    assert_equal(default, parallel_checks_jobs)
    # GNU make 3.81 to 4.1 do not pass the number to sub-makes
    ENV["MAKEFLAGS"] = " --jobserver-fds=3,4 -j"
    assert_equal(default, parallel_checks_jobs)
    ENV["MAKEFLAGS"] = "k --jobserver-auth=fifo:/tmp/GMfifo1"
    assert_equal(default, parallel_checks_jobs)
    ENV["MAKEFLAGS"] = "k"
    $mflags = %w[-j2]           # extmk.rb --mflags=-j2
    assert_equal(2, parallel_checks_jobs)
    ENV["MAKEFLAGS"] = " -j --jobserver-fds=3,4"
    assert_equal(2, parallel_checks_jobs)
    $mflags = mflags
    ENV["MAKEFLAGS"], ENV["MAKELEVEL"] = nil, nil
    assert_equal(default, parallel_checks_jobs)
  ensure
    $mflags = mflags
    ENV["MAKEFLAGS"], ENV["MAKELEVEL"] = makeflags, makelevel
  end

  def test_error_in_check
    assert_raise_with_message(RuntimeError, "error in check") do
      mkmf {parallel_checks(4) {|c|
          c.have_func("ruby_init")
          c.checking_for("error") {raise "error in check"}
          c.have_func("ruby_init")
        }}
    end
    assert_equal(["-DHAVE_RUBY_INIT"], $defs)
    assert_empty(conftest_files)
  end

  def test_unknown_method
    assert_raise(NoMethodError) do
      parallel_checks(4) {|c| c.no_such_check}
    end
  end
end
