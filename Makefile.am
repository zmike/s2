ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

EXTRA_DIST = \
AUTHORS \
COPYING

bin_PROGRAMS =
DISTCLEANFILES =
BUILT_SOURCES = 
MAINTAINERCLEANFILES = \
Makefile.in \
aclocal.m4 \
config.guess \
config.h* \
config.sub \
configure \
depcomp \
install-sh \
ltmain.sh \
missing \
compile \
m4/l* \
ylwrap

INTERMEDIATE_S =

include src/bin/Makefile.mk
include src/modules/Makefile.mk

if MOD_LIST_BASIC
include data/themes/list_basic.mk
endif

maintainer-clean-local:
	rm -rf autom4te.cache
