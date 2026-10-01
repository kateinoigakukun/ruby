dnl -*- Autoconf -*-
AC_DEFUN([RUBY_TRY_CXXFLAGS], [
    save_CXXFLAGS="$CXXFLAGS"
    CXXFLAGS="[$]CXXFLAGS $1"
    AC_MSG_CHECKING([whether ]$1[ is accepted as CXXFLAGS])
    RUBY_WERROR_FLAG([
    AC_LANG_PUSH([C++])
    rb_cxx_werror_flag=$ac_cxx_werror_flag
    ac_cxx_werror_flag=$ac_c_werror_flag
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[$4]], [[$5]])],
	[$2
	AC_MSG_RESULT(yes)],
	[$3
	AC_MSG_RESULT(no)])
    ac_cxx_werror_flag=$rb_cxx_werror_flag
    ])
    AC_LANG_POP([C++])
    CXXFLAGS="$save_CXXFLAGS"
    save_CXXFLAGS=
])dnl
