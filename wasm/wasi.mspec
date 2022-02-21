# -*- ruby -*-
require 'mspec/helpers/tmp'
require 'fileutils'

unsupported_methods = [
  [Thread.singleton_class, [:new]],
  [Kernel, [:system, :spawn, :fork, :`]],
  [Kernel.singleton_class, [:system, :spawn, :fork, :`]],
  [Process.singleton_class, [:spawn, :fork]],
  [File, [:chmod, :chown, :flock]],
  [File.singleton_class, [:chmod, :chown, :symlink, :mkfifo]],
  [IO.singleton_class, [:pipe]],
]

unsupported_methods.each do |klass, methods|
  klass.class_exec do
    methods.each do |method|
      original = instance_method(method)
      define_method(method) do |*args, &block|
        raise SkippedSpecError, "WASI doesn't support #{klass}##{method}" if MSpec.current
        original.bind(self).call(*args, &block)
      end
    end
  end
end

class MSpecScript

  builddir = Dir.pwd
  srcdir = File.dirname(File.dirname(__FILE__))
  specdir = File.join(srcdir, "spec")

  set :prefix, File.join(specdir, "ruby")

  set :files, [
    'language',
    'core',
#   UNSUPPORTED: WASI doesn't have pipe, and read_nonblock spec depends on it
    '^' + File.join(specdir, 'ruby/core/argf/read_nonblock_spec.rb'),
#   FIXME(katei): wasi-libc's chdir has BOF, and the effect was that it
#   occasionally inserted random bytes into paths.
#   * link: https://github.com/WebAssembly/wasi-libc/pull/247
    '^' + File.join(specdir, 'ruby/core/dir'),
#   UNSUPPORTED: wasi-libc's strerror returns "Success" when an invalid errno is given.
#   This behavior originally came from musl.
    '^' + File.join(specdir, 'ruby/core/exception/system_call_error_spec.rb'),
    File.join(specdir, 'ruby/core/false'),
    File.join(specdir, 'ruby/core/fiber'),
    File.join(specdir, 'ruby/core/file'),
    File.join(specdir, 'ruby/core/filetest'),
#   UNSUPPORTED: WASI doesn't have traditional permission and device concept
    '^' + File.join(specdir, 'ruby/core/file/chmod_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/chown_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/grpowned_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/identical_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/lchown_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/sticky_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/umask_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/blksize_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/dev_major_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/dev_minor_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/grpowned_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/nlink_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/rdev_major_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/rdev_minor_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/stat/sticky_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/filetest/grpowned_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/filetest/identical_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/filetest/sticky_spec.rb'),
#   FIXME(katei): cap-std, an underlying sandbox implementation of WASI in wasmtime, doesn't
#   allow to create a symlink to an absolute path to enforce sandbox restriction.
#   * link: https://github.com/bytecodealliance/cap-std/commit/257867a1d3a589b2561b00111ffa4db3bab0e8be
    '^' + File.join(specdir, 'ruby/core/file/symlink_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/link_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/realpath_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/realdirpath_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/ftype_spec.rb'),
#   FIXME(katei): cap-std, an underlying sandbox implementation of WASI in wasmtime, checks oflags
#   very differently from other platforms, and most of specs depending on it are meaningless on WASI
    '^' + File.join(specdir, 'ruby/core/file/open_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/file/new_spec.rb'),
#   FIXME(katei): wasi-libc doesn't provide dup family, but has fd_renumber, which does
#   a move rather than a copy, so reopen can be implemented in theory.
    '^' + File.join(specdir, 'ruby/core/file/reopen_spec.rb'),
#   UNSUPPORTED: WASI doesn't have a way to open a new socket
    '^' + File.join(specdir, 'ruby/core/file/socket_spec.rb'),
#   FIXME(katei): WASI doesn't have cwd concept, and Dir.pwd always returns "/".
#   But expand_path_spec doesn't take into account the case where cwd is "/"
    '^' + File.join(specdir, 'ruby/core/file/expand_path_spec.rb'),
#   UNSUPPORTED: WASI doesn't have pipe, and most of io specs depends on it
    '^' + File.join(specdir, 'ruby/core/io'),
#   UNSUPPORTED: WASI doesn't support spawning a new process
    '^' + File.join(specdir, 'ruby/core/kernel/system_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/kernel/spawn_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/kernel/fork_spec.rb'),
    '^' + File.join(specdir, 'ruby/core/kernel/backtick_spec.rb'),
#   FIXME(katei): stdout matcher uses dup, but WASI doesn't have dup
    '^' + File.join(specdir, 'ruby/core/kernel/p_spec.rb'),
#   UNSUPPORTED: WASI doesn't support spawning a new thread
    '^' + File.join(specdir, 'ruby/core/mutex'),
#   UNSUPPORTED: WASI doesn't support spawning a new process
    '^' + File.join(specdir, 'ruby/core/process'),
#   UNSUPPORTED: WASI doesn't support spawning a new thread
    '^' + File.join(specdir, 'ruby/core/thread'),
    '^' + File.join(specdir, 'ruby/core/threadgroup'),
  ]

  set :ci_files, get(:files)

  # tmp.rb tries to create a tmpdir under root dir because
  # pwd is always / in WASI, so prepare tmpdir before executing
  # the spec
  FileUtils.mkdir_p SPEC_TEMP_DIR
  set :target, "wasmtime run --env RUBY_FIBER_MACHINE_STACK_SIZE=#{12 * 64 * 1024} --env SPEC_TEMP_DIR=#{SPEC_TEMP_DIR} --env HOME=#{ENV['HOME']} --env RUBY_EXE=#{builddir}/ruby --mapdir /::/ #{builddir}/ruby -- -r#{builddir}/rbconfig.rb -I#{srcdir}/lib -I#{builddir}"
end
