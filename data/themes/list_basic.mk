EDJE_FLAGS = -id $(top_srcdir)/data/themes

list_basic_filesdir = $(datadir)/s2
list_basic_files_DATA = data/themes/list_basic.edj

images = \
data/themes/arrows_both.png \
data/themes/arrows_pending_left.png \
data/themes/arrows_pending_right.png \
data/themes/arrows_rejected.png \
data/themes/userunknown.png \
data/themes/x.png

EXTRA_DIST += \
data/themes/list_basic.edc \
$(images)

data/themes/list_basic.edj: $(images) data/themes/list_basic.edc
	@edje_cc@ $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/list_basic.edc \
	$(top_builddir)/data/themes/list_basic.edj
