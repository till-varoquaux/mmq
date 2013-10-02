AC_PREREQ([2.59])
AC_INIT([mmq], [0.1], [till@okcupid.com], [mmq], [https://github.com/till-varoquaux/mmq])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([mmq.h])

AM_INIT_AUTOMAKE([foreign dist-bzip2])

AC_PROG_INSTALL

AC_PROG_LIBTOOL
LT_INIT

AC_LANG([C++])
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])
AX_APPEND_COMPILE_FLAGS([-Wall -Wextra -Wpedantic -Wdocumentation -Wextra-semi -Wno-zero-length-array -Wno-c++11-extensions -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-weak-vtables -Wno-documentation-unknown-command], [CXXFLAGS], [])

AC_HEADER_STDC

AC_CHECK_HEADERS([sys/mman.h unistd.h sys/types.h sys/stat.h fcntl.h error.h],,AC_MSG_ERROR([oops! Missing header]))

AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([[ extern "C" {
#include <unistd.h>
#include <errno.h>
}]],
[[ (void) TEMP_FAILURE_RETRY(0) ]]
)],
  [],
  [AC_MSG_ERROR([Could not find the TEMP_FAILURE_RETRY macro. Are you using glibc?])])

AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

AC_CHECK_FUNCS([mremap],,AC_MSG_ERROR([oops! Missing function]))

AC_PROG_INSTALL

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
