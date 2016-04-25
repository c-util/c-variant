# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

pkgconfiglibdir=$(libdir)/pkgconfig

BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST =
include_HEADERS =
pkgconfiglib_DATA =
noinst_LIBRARIES =
check_PROGRAMS =
TESTS =
default_tests =

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS) $(GLIB_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# library archive

noinst_LIBRARIES += \
	libcvariant.a

libcvariant_a_SOURCES = \
	src/c-variant.c \
	src/c-variant-private.h \
	src/c-variant-reader.c \
	src/c-variant-writer.c \
	src/libcvariant.sym \
	src/c-variant.h

if HAVE_GMP
libcvariant_a_SOURCES += \
	src/generator.c \
	src/generator.h
endif

# ------------------------------------------------------------------------------
# shared library built from archive

all-local: libcvariant.so.0

include_HEADERS += \
	src/c-variant.h

libcvariant.so.0: libcvariant.a $(top_srcdir)/src/libcvariant.sym
	$(AM_V_CCLD)$(LINK) -shared \
	-Wl,-soname=$@ \
	-Wl,--version-script=$(top_srcdir)/src/libcvariant.sym \
	-Wl,--whole-archive libcvariant.a -Wl,--no-whole-archive

CLEANFILES += \
	libcvariant.so.0

install-exec-local:
	@echo " $(MKDIR_P) '$(DESTDIR)$(libdir)'"; \
		$(MKDIR_P) "$(DESTDIR)$(libdir)" || exit 1
	@echo " $(INSTALL) libcvariant.so.0 '$(DESTDIR)$(libdir)'"; \
		$(INSTALL) libcvariant.so.0 "$(DESTDIR)$(libdir)" || exit $$?
	@echo " $(LN_S) -f libcvariant.so.0 '$(DESTDIR)$(libdir)/libcvariant.so'"; \
		$(LN_S) -f libcvariant.so.0 "$(DESTDIR)$(libdir)/libcvariant.so" || exit $$?

uninstall-local:
	@test ! -d "$(DESTDIR)$(libdir)" || \
		{ echo " ( cd '$(DESTDIR)$(libdir)' && rm -f libcvariant.so.0 )"; \
		  rm -f "$(DESTDIR)$(libdir)/libcvariant.so.0"; }

%.pc: %.pc.in
	$(AM_V_GEN)$(SED) \
		-e 's,@includedir\@,$(includedir),g' \
		-e 's,@libdir\@,$(libdir),g' \
		-e 's,@VERSION\@,$(VERSION),g' \
		$< > $@

pkgconfiglib_DATA += \
	src/c-variant.pc

EXTRA_DIST += \
	src/c-variant.pc.in

CLEANFILES += \
	src/c-variant.pc

# ------------------------------------------------------------------------------
# test-api

default_tests += \
	test-api

test_api_SOURCES = \
	src/test-api.c

test_api_LDADD = \
	libcvariant.so.0 # explicitly linked against public library

# ------------------------------------------------------------------------------
# test-generator

if HAVE_GMP
default_tests += \
	test-generator
endif

test_generator_SOURCES = \
	src/test-generator.c

test_generator_LDADD = \
	libcvariant.a

# ------------------------------------------------------------------------------
# test-glib

if HAVE_GMP
if HAVE_GLIB
default_tests += \
	test-glib
endif
endif

test_glib_SOURCES = \
	src/test-glib.c

test_glib_LDADD = \
	libcvariant.a \
	$(GLIB_LIBS)

# ------------------------------------------------------------------------------
# test-perf

default_tests += \
	test-perf

test_perf_SOURCES = \
	src/test-perf.c

test_perf_LDADD = \
	libcvariant.a

# ------------------------------------------------------------------------------
# test-reader

default_tests += \
	test-reader

test_reader_SOURCES = \
	src/test-reader.c

test_reader_LDADD = \
	libcvariant.a

# ------------------------------------------------------------------------------
# test-signature

default_tests += \
	test-signature

test_signature_SOURCES = \
	src/test-signature.c

test_signature_LDADD = \
	libcvariant.a

# ------------------------------------------------------------------------------
# test-writer

default_tests += \
	test-writer

test_writer_SOURCES = \
	src/test-writer.c

test_writer_LDADD = \
	libcvariant.a

# ------------------------------------------------------------------------------
# test suite

AM_TESTS_ENVIRONMENT = LD_LIBRARY_PATH=$(abs_builddir)
check_PROGRAMS += $(default_tests)
TESTS += $(default_tests)

# ------------------------------------------------------------------------------¶
# check "make install" directory tree

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree
