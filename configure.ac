##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [0])
m4_define([v_min], [0])
m4_define([v_mic], [1])
m4_define([v_rev], m4_esyscmd([(git rev-list --count HEAD 2>/dev/null || echo 0) | tr -d '\n']))dnl
##--   When released, remove the dnl on the below line
dnl m4_undefine([v_rev])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], [m4_define([v_ver], [v_maj.v_min.v_mic.v_rev])],
[m4_define([v_ver], [v_maj.v_min.v_mic])])
m4_define([lt_rev], m4_eval(v_maj + v_min))
m4_define([lt_cur], v_mic)
m4_define([lt_age], v_min)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##

AC_INIT([S2], [v_ver], [discomfitor@efl.so], [S2])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])

AC_GNU_SOURCE
AC_ISC_POSIX

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], , [m4_define([v_rev], [0])])
m4_ifdef([v_rel], , [m4_define([v_rel], [])])
AC_DEFINE_UNQUOTED(VMAJ, [v_maj], [Major version])
AC_DEFINE_UNQUOTED(VMIN, [v_min], [Minor version])
AC_DEFINE_UNQUOTED(VMIC, [v_mic], [Micro version])
AC_DEFINE_UNQUOTED(VREV, [v_rev], [Revison])
version_info="lt_rev:lt_cur:lt_age"
release_info="v_rel"
AC_SUBST(version_info)
AC_SUBST(release_info)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
VMAJ=v_maj
AC_SUBST(VMAJ)

AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_MAKE_SET
AC_HEADER_STDC

AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

AC_FUNC_ALLOCA
AC_CHECK_FUNCS([strdup strndup])

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([EFL], [ecore ecore-con shotgun])

##############################

AC_ARG_WITH([edje-cc],
   [AC_HELP_STRING([--with-edje-cc=PATH],
                   [specify a specific path to edje_cc])],
   [edje_cc=$withval;
    AC_MSG_NOTICE([edje_cc explicitly set to $edje_cc])
   ],[edje_cc=$(pkg-config --variable=prefix edje)/bin/edje_cc])
AC_SUBST(edje_cc)

have_eldbus=
want_eldbus=
AC_ARG_ENABLE([eldbus],
   [AC_HELP_STRING([--disable-eldbus], [disable eldbus support. @<:@default=detect@:>@])],
   [want_eldbus=$enableval], [])

if test "x$want_eldbus" != "xno"; then
   PKG_CHECK_MODULES([ELDBUS],
      [eldbus],
      [
       AC_DEFINE(HAVE_DBUS, 1, [DBUS support])
       have_eldbus="yes"
      ],
      [have_eldbus=]
   )
else
    have_eldbus=
fi
if test "x$want_eldbus" = "xyes" ; then
   if test -n "$have_eldbus" ; then
      AC_MSG_ERROR([eldbus support requested, but not found by pkg-config.])
   fi
fi

AC_ARG_WITH([eldbus-codegen],
   [AC_HELP_STRING([--with-eldbus-codegen=PATH],
                   [specify a specific path to eldbus-codegen])],
   [eldbus_codegen=$withval;
    AC_MSG_NOTICE([eldbus-codegen explicitly set to $eldbus_codegen])
   ],[eldbus_codegen=$(pkg-config --variable=prefix eldbus)/bin/eldbus-codegen])
AC_SUBST(eldbus_codegen)

############# MODULES ###################
want_dbus_conn_mod=maybe
AC_ARG_ENABLE([module-dbus-connection],
   [AC_HELP_STRING([--disable-module-dbus-connection], [disable dbus connection module. @<:@default=detect@:>@])],
   [want_dbus_conn_mod=$enableval], [])
if test "x$want_dbus_conn_mod" = "xyes" && test -n "$have_eldbus" ; then
   AC_MSG_ERROR([dbus module requested, but not eldbus support not available])
fi
want_dbus_conn_mod=$have_eldbus
AM_CONDITIONAL([MOD_DBUS_CONN], [test "x$want_dbus_conn_mod" = "xyes"])

want_dummy_login=yes
AC_ARG_ENABLE([module-login-dummy],
   [AC_HELP_STRING([--disable-module-login-dummy], [disable dummy login module. @<:@default=enabled@:>@])],
   [want_dummy_login=$enableval], [])
AM_CONDITIONAL([MOD_DUMMY_LOGIN], [test "x$want_dummy_login" = "xyes"])

########################################

case "$host_os" in
   mingw32ce*)
      MODULE_ARCH="$host_os-$host_cpu"
      ;;
   *)
      MODULE_ARCH="$host_os-$host_cpu-v_maj.v_min.v_mic"
      ;;
esac
AC_SUBST(MODULE_ARCH)
AC_DEFINE_UNQUOTED([MODULE_ARCH], "${MODULE_ARCH}", "Module architecture")

case "$host_os" in
   mingw*)
      MODULE_EXT=".dll"
      ;;
   *)
      MODULE_EXT=".so"
      ;;
esac

AC_DEFINE_UNQUOTED([SHARED_LIB_SUFFIX], "${MODULE_EXT}", [Suffix for shared objects])

m4_ifdef([v_rev],
   [
    EFL_COMPILER_FLAG([-Wshadow])
    EFL_COMPILER_FLAG([-Wall])
    EFL_COMPILER_FLAG([-Wextra])
   ])

AC_CONFIG_FILES([
  Makefile
])
AC_OUTPUT

# report
txt_strip() {
  echo "[$]@" | sed -e 's/^[[ \t]]*\([[^ \t]]*\)[[ \t]]*$/\1/g'
}

echo
cat << SUMMARY_EOF
Summary:
 * project.........: $PACKAGE $VERSION
 * prefix..........: $(txt_strip $prefix)
 * CFLAGS..........: $(txt_strip $CFLAGS)
 * LDFLAGS.........: $(txt_strip $LDFLAGS)
SUMMARY_EOF
echo
cat << FEATURES_EOF
Features:
 * dbus................: $have_eldbus
FEATURES_EOF
echo

cat << MODULES_EOF
Modules:
 * dummy_login.........: $want_dummy_login
 * dbus_conn...........: $want_dbus_conn_mod
MODULES_EOF
echo

cat << COMPILE_EOF
Compilation........: make (or gmake)
COMPILE_EOF
echo

cat << INSTALL_EOF
Installation.......: make all install (as root if needed, with 'su' or 'sudo')
INSTALL_EOF
echo
