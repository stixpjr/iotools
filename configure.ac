dnl $Id$
dnl
dnl configure.in:
dnl Process this file with autoconf to produce a configure script.

AC_INIT(iotools, 20060417, stix@stix.id.au)
AC_REVISION($Revision$)dnl

AC_CONFIG_HEADER(config.h)
AC_CONFIG_AUX_DIR(config)
dnl AM_INIT_AUTOMAKE(iotools,IOTOOLS_VERSION)

dnl Version string
AC_SUBST(IOTOOLS_VERSION)
AC_MSG_NOTICE([Configuring iotools $IOTOOLS_VERSION])
AC_DEFINE_UNQUOTED(IOTOOLS_VERSION, ["$IOTOOLS_VERSION"], [iotools release version])

dnl AC_ARG_PROGRAM
AC_ARG_ENABLE(pthreads, [
  --enable-pthreads       Attempt to compile with pthread support (recommended)
  --disable-pthreads      Use older, slower, multi-process code
                          [default: enabled].],
    [use_pthreads=$enableval],
    [use_pthreads=yes])

dnl Checks for programs.
AC_PROG_MAKE_SET
if test $use_pthreads = yes; then
	AC_PROG_CC([gcc cc_r cc])
else
	AC_PROG_CC([gcc cc])
fi
AC_PROG_INSTALL
AC_PROG_CC_STDC
AC_AIX

if test $ac_cv_c_compiler_gnu = yes; then
	CFLAGS="${CFLAGS} -Wall"
fi

dnl Bail out if we don't have ANSI C
if test "x$ac_cv_prog_cc_stdc" = xno
then
	AC_MSG_WARN([iotools requires an ANSI C compiler and you don't seem to have one])
fi

if test $use_pthreads = yes; then
	dnl Does this system even have pthreads?
	dnl Just look for the lib, not a specific function, since some
	dnl OSes (Tru64 4.0D) break POSIX and mangle function names
	dnl with macros
	AC_CHECK_LIB(pthread, main)
	if test $ac_cv_lib_pthread_main = no; then
		AC_CHECK_LIB(pthreads, main)
		if test $ac_cv_lib_pthreads_main = no; then
			AC_MSG_WARN([pthread library not found, continuing anyway])
		fi
	fi
	AC_DEFINE(USE_PTHREADS, 1, [Define to attempt to use pthreads])
	AC_SUBST(PROGS, ["fblckgen iohammer mbdd"])
else
	AC_SUBST(PROGS, ["fblckgen iohammer"])
	dnl Non-pthread specific tests
	AC_CHECK_HEADER([sys/shm.h])
	AC_FUNC_MMAP
	AC_FUNC_SELECT_ARGTYPES
fi

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_HEADERS([ctype.h getopt.h errno.h fcntl.h inttypes.h limits.h sys/ioctl.h sys/time.h unistd.h])

dnl Prefer largefile support
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE
AC_CHECK_SIZEOF(off_t, 0)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_VOLATILE
AC_C_INLINE
AC_TYPE_SIGNAL
AC_CHECK_TYPES([long long, unsigned long long, int64_t])

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_FORK
AC_FUNC_FSEEKO
dnl AC_FUNC_MALLOC
AC_FUNC_STAT
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS(bzero memset, break)
AC_CHECK_FUNCS(bcopy memcpy, break)
AC_CHECK_FUNCS([gettimeofday select strerror])

# Check for some variables
AC_MSG_CHECKING([for optarg declaration])
AC_TRY_LINK([
#include <stdlib.h>
#include <unistd.h>
    ], [
        char *X = optarg
    ], [
        AC_MSG_RESULT(yes)
        HAVE_OPTARG_D=yes
    ], [
        AC_MSG_RESULT(no)
    ])

dnl If we don't have inttypes.h, work out how to printf
dnl long long, int64_t, and define PRId64 appropriately
can_printf_longlong=no
if test $ac_cv_header_inttypes_h = no -a $ac_cv_type_long_long = yes; then
	dnl try %lld
	AC_MSG_CHECKING([*printf() support for %lld])
	AC_TRY_RUN([
#include <stdio.h>
int main() {
	char buf[100];
	sprintf(buf, "%lld", 4294967300LL);
	return (strcmp(buf, "4294967300"));
}
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE([PRId64], ["lld"], printf supports lld)
		can_printf_longlong=yes
	], [
		AC_MSG_RESULT(no)
	], [ : ])

	if test $can_printf_longlong != yes; then
		AC_MSG_CHECKING([*printf() support for %qd])
		AC_TRY_RUN([
#include <stdio.h>
int main() {
	char buf[100];
	sprintf(buf, "%qd", 4294967300LL);
	return (strcmp(buf, "4294967300"));
}
		], [
			AC_MSG_RESULT(yes)
			can_printf_longlong=yes
			AC_DEFINE([PRId64], ["qd"], printf supports qd)
		], [
			AC_MSG_RESULT(no)
		], [ : ])
	fi
	if test $can_printf_longlong = no; then
		AC_MSG_WARN(["lld" and "qd" not supported, defining PRId64 to "ld"])
		AC_DEFINE([PRId64], ["ld"], PRId64 not supported)
	fi
fi

AC_OUTPUT(Makefile)