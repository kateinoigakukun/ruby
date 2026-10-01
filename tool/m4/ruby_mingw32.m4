dnl -*- Autoconf -*-
AC_DEFUN([RUBY_MINGW32],
[AS_CASE(["$host_os"],
[cygwin*], [
AC_CACHE_CHECK(for mingw32 environment, rb_cv_mingw32,
[AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
#ifndef __MINGW32__
# error
#endif
]])],[rb_cv_mingw32=yes],[rb_cv_mingw32=no])
rm -f conftest*])
AS_IF([test "$rb_cv_mingw32" = yes], [
    target_os="mingw32"
    : ${ac_tool_prefix:="`expr "$CC" : ['\(.*-\)g\?cc[^/]*$']`"}
    AC_DEFINE(__USE_MINGW_ANSI_STDIO, 1) dnl for gnu_printf
])
])
AS_CASE(["$target_os"], [mingw*msvc], [
target_os="`echo ${target_os} | sed 's/msvc$//'`"
])
AS_CASE(["$target_cpu-$target_os"], [x86_64-mingw*], [
target_cpu=x64
])
dnl Visual C++, from the shell of Cygwin or MSYS2, or for the target
dnl *-*-windows-msvc.  The target names follow win32/setup.mak, which
dnl names the OS after _WIN64 and the CPU after _M_ARM64, _M_X64 or
dnl _M_IX86; the runtime version is appended later.
AS_CASE(["$target_os"], [cygwin*|msys*|windows*], [
AC_CACHE_CHECK(for Visual C++, rb_cv_msvc,
[AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
#ifndef _MSC_VER
# error
#endif
]])],[rb_cv_msvc=yes],[rb_cv_msvc=no])])
AS_IF([test "$rb_cv_msvc" = yes], [
    AC_CACHE_CHECK(target machine of Visual C++, rb_cv_msvc_machine, [
	for rb_cv_msvc_machine in _M_ARM64:arm64 _M_X64:x64 _M_IX86:i386 no; do
	    AS_CASE([$rb_cv_msvc_machine], [no], [break])
	    AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
@%:@ifndef ${rb_cv_msvc_machine%%:*}
@%:@ error
@%:@endif
]])], [rb_cv_msvc_machine=${rb_cv_msvc_machine@%:@*:}; break])
	done])
    AS_CASE([$rb_cv_msvc_machine], [no], [AC_MSG_ERROR([unknown target machine of Visual C++])])
    target_cpu=$rb_cv_msvc_machine
    AS_CASE([$rb_cv_msvc_machine], [i386], [target_os=mswin32], [target_os=mswin64])
])
])
])dnl
