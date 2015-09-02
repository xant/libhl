#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libhl], m4_esyscmd_s([build-aux/git-version-gen]), [https://github.com/xant/libhl/issues])


AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/atomic_defs.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/m4_ax_pthread.m4])

LT_INIT

AC_SUBST([LIBTOOL_DEPS])

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/param.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRCOLL
AX_PTHREAD( [
            AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.])
            CLIBS="$PTHREAD_LIBS $LIBS"
            CFLAGS="$CFLAGS $PTHREAD_CFLAGS -DTHREAD_SAFE"
            CC="$PTHREAD_CC"],[])

# Not all compilers include /usr/local in the include and link path
if test -d /usr/local/include; then
        CFLAGS="$CFLAGS -I/usr/local/include"
            LDFLAGS="$LDFLAGS -L/usr/local/lib"
        fi
AC_CHECK_FUNCS([gettimeofday memchr memmove memset stpcpy strchr strcspn strdup strerror strpbrk strrchr strspn strstr strtol])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
