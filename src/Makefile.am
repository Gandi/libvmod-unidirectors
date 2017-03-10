AM_CPPFLAGS = @VMOD_INCLUDES@ -Wall -Werror

vmoddir = @VMOD_DIR@
vmod_LTLIBRARIES = libvmod_unidirectors.la

libvmod_unidirectors_la_LDFLAGS = -module -export-dynamic -avoid-version -shared

libvmod_unidirectors_la_SOURCES = \
	vmod_unidirectors.c \
	dynamic.c \
	udir.c \
	udir.h \
	fall_back.c \
	hash.c \
	random.c \
	round_robin.c \
	least_conn.c

nodist_libvmod_unidirectors_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

vmod_unidirectors.lo: vcc_if.h

vcc_if.h vmod_unidirectors.rst vmod_unidirectors.man.rst: vcc_if.c

vcc_if.c: @VMODTOOL@ $(top_srcdir)/src/vmod_unidirectors.vcc
	@VMODTOOL@ $(top_srcdir)/src/vmod_unidirectors.vcc

VMOD_TESTS = $(top_srcdir)/src/tests/*.vtc
.PHONY: $(VMOD_TESTS)

$(top_srcdir)/src/tests/*.vtc: libvmod_unidirectors.la
	@VARNISHTEST@ -Dvarnishd=@VARNISHD@ -Dvmod_topbuild=$(abs_top_builddir) $@

check: $(VMOD_TESTS)

EXTRA_DIST = \
	vmod_unidirectors.vcc \
	$(VMOD_TESTS)

CLEANFILES = \
	$(builddir)/vcc_if.c \
	$(builddir)/vcc_if.h \
	$(builddir)/vmod_unidirectors.rst \
	$(builddir)/vmod_unidirectors.man.rst
