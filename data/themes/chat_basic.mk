EDJE_FLAGS = -id $(top_srcdir)/data/themes

chat_basic_filesdir = $(datadir)/s2
chat_basic_files_DATA = data/themes/chat_basic.edj

images = \
data/themes/userunknown.png

EXTRA_DIST += \
data/themes/chat_basic.edc \
$(images)

data/themes/chat_basic.edj: $(images) data/themes/chat_basic.edc
	@edje_cc@ $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/chat_basic.edc \
	$(top_builddir)/data/themes/chat_basic.edj
