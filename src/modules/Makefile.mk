moddir = $(libdir)/s2/$(MODULE_ARCH)

EXTRA_DIST += src/modules/dbus_conn.xml
mod_LTLIBRARIES =

if MOD_DUMMY_LOGIN
mod_LTLIBRARIES += src/modules/dummy_login.la

src_modules_dummy_login_la_SOURCES = \
src/modules/dummy_login.c \
src/modules/getpass_x.c

src_modules_dummy_login_la_CPPFLAGS = \
$(AM_CFLAGS) \
@EFL_CFLAGS@ \
-I$(top_srcdir)/src/bin \
-I$(top_builddir)

src_modules_dummy_login_la_LIBADD = \
@EFL_LIBS@

src_modules_dummy_login_la_LDFLAGS = -module -avoid-version

src_modules_dummy_login_la_LIBTOOLFLAGS = --tag=disable-static
endif

if MOD_DBUS_CONN

mod_LTLIBRARIES += src/modules/dbus_conn.la

src_modules_dbus_conn_la_SOURCES = \
src/modules/dbus_conn.c

src_modules_dbus_conn_la_CPPFLAGS = \
$(AM_CFLAGS) \
@EFL_CFLAGS@ \
@ELDBUS_CFLAGS@ \
-I$(top_srcdir)/src/bin \
-I$(top_builddir)

src_modules_dbus_conn_la_LIBADD = \
@EFL_LIBS@ \
@ELDBUS_LIBS@

src_modules_dbus_conn_la_LDFLAGS = -module -avoid-version

src_modules_dbus_conn_la_LIBTOOLFLAGS = --tag=disable-static

endif
