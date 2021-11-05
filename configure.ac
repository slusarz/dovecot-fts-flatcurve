AC_INIT([Dovecot FTS Flatcurve], [0.1.0], [slusarz@curecanti.org], [dovecot-fts-flatcurve])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])

AC_DEFINE_UNQUOTED(FTS_FLATCURVE_NAME, "$PACKAGE_NAME",
    [Define to the full name of Dovecot FTS Flatcurve.])
AC_DEFINE_UNQUOTED(FTS_FLATCURVE_VERSION, "$PACKAGE_VERSION",
    [Define to the version of Dovecot FTS Flatcurve.])

AM_INIT_AUTOMAKE([no-define foreign tar-ustar])
AM_MAINTAINER_MODE
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_LIBTOOL

PKG_PROG_PKG_CONFIG

DC_DOVECOT
DC_DOVECOT_CFLAGS
DC_DOVECOT_HARDENING
DC_DOVECOT_MODULEDIR
LIBDOVECOT_INCLUDE="$LIBDOVECOT_INCLUDE"
CFLAGS="$CFLAGS $EXTRA_CFLAGS"
CXXFLAGS="$CXXFLAGS $EXTRA_CXXFLAGS"
LIBS="$DOVECOT_LIBS"
AC_SUBST(LIBDOVECOT_INCLUDE)

XO_LIB_XAPIAN
AS_VERSION_COMPARE([1.3.99], [$XAPIAN_VERSION],
		   AC_DEFINE([XAPIAN_HAS_COMPACT],[1],[Xapian compaction support (1.4+)]))

AC_MSG_CHECKING([for fts_mail_user_init API version 2.3.17+])
AC_LANG_PUSH(C)
ac_save_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -DHAVE_CONFIG_H $LIBDOVECOT_INCLUDE $LIBDOVECOT_STORAGE_INCLUDE $LIBDOVECOT_FTS_INCLUDE"
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM(
        [[#undef PACKAGE_BUGREPORT]
         [#undef PACKAGE_NAME]
         [#undef PACKAGE_STRING]
         [#undef PACKAGE_TARNAME]
         [#undef PACKAGE_VERSION]
         [#include "lib.h"]
         [#include "mail-storage-hooks.h"]
         [#include "fts-user.h"]],
        [[int (*ptr)(struct mail_user *, bool, const char **), i = 0;]
         [ptr = fts_mail_user_init;]
         [if (ptr && i) ++i;]])
    ],[
        AC_MSG_RESULT([yes])
        AC_DEFINE(HAVE_FTS_MAIL_USER_INIT_2_3_17, [], [fts_mail_user_init API version 2.3.17+])
    ],[
        AC_MSG_RESULT([no])
    ])
CFLAGS=$ac_save_CFLAGS
AC_LANG_POP()

if test "$DOVECOT_INSTALLED" != "yes"; then
    DOVECOT_FTS_PLUGIN="$abs_dovecotdir/src/plugins/fts/lib20_fts_plugin.la"
else
    DOVECOT_FTS_PLUGIN="$dovecot_installed_moduledir/lib20_fts_plugin.la"
fi
AC_SUBST(DOVECOT_FTS_PLUGIN)

AC_CONFIG_HEADERS([dummy-config.h fts-flatcurve-config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
stamp.h])

AC_OUTPUT

