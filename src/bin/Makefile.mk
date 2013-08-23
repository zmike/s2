bin_PROGRAMS += src/bin/s2

src_bin_s2_CPPFLAGS = \
$(AM_CFLAGS) \
-I$(top_builddir) \
@EFL_CFLAGS@ \
-DDATA_DIR=\"$(datadir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/s2\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_SRC_DIR=\"$(top_srcdir)\" \
-DUI_MODULE_PATH=\"$(libdir)/s2/$(MODULE_ARCH)\"

src_bin_s2_LDFLAGS = -rdynamic

src_bin_s2_LDADD = \
@EFL_LIBS@

src_bin_s2_SOURCES = \
src/bin/main.c
