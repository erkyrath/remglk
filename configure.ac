# configure.ac
#
# This file is part of remglk.  See README.txt and LICENSE for details.

AC_PREREQ([2.69])

AC_INIT(
    [remglk],
    [0.2.7],
    [erkyrath@eblong.com],
    [remglk],
    [http://eblong.com/zarf/glk/remglk/docs.html])

AM_INIT_AUTOMAKE([foreign -Wall])
AM_PROG_AR
LT_INIT

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h strings.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([bzero floor getcwd gettimeofday localtime_r memmove setenv strdup tzset])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
