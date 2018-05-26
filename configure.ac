AC_INIT(ttyml,1.0.0)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_CHECK_HEADERS([sys/ioctl.h unistd.h])

PKG_CHECK_MODULES([CURL], [libcurl])
PKG_CHECK_MODULES([EXPAT], [expat])

AC_CONFIG_HEADERS([config.h])
AC_OUTPUT(Makefile)
