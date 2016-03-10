# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

SUBDIRS = .

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

pkgincludedir=$(includedir)/org.bus1
pkgconfiglibdir=$(libdir)/pkgconfig

BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST =
pkginclude_HEADERS =
pkgconfiglib_DATA =
noinst_LIBRARIES =
check_PROGRAMS = $(default_tests)
TESTS = $(default_tests)
default_tests =

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# libcvariant.a

noinst_LIBRARIES += \
	libcvariant.a

pkginclude_HEADERS += \
	src/c-variant.h

libcvariant_a_SOURCES = \
	src/libcvariant.sym \
	src/c-variant.h \
	src/c-variant-private.h \
	src/c-variant-reader.c \
	src/c-variant-writer.c \
	src/c-variant.c

if HAVE_GMP
libcvariant_a_SOURCES += \
	src/generator.h \
	src/generator.c
endif

# ------------------------------------------------------------------------------
# libcvariant.so.0

libcvariant.so.0: libcvariant.a $(top_srcdir)/src/libcvariant.sym
	$(AM_V_CCLD)$(CCLD) $(AM_CFLAGS) $(AM_LDFLAGS) \
	-shared \
	-Wl,-soname=$@ \
	-Wl,--version-script=$(top_srcdir)/src/libcvariant.sym \
	-Wl,--whole-archive libcvariant.a -Wl,--no-whole-archive \
	-o $@

CLEANFILES += \
	libcvariant.so.0

all-local: libcvariant.so.0

install-exec-hook:
	$(MKDIR_P) $(DESTDIR)$(libdir)
	cp libcvariant.so.0 $(DESTDIR)$(libdir)
	$(AM_V_LN)ln -sf libcvariant.so.0 $(DESTDIR)$(libdir)/libcvariant.so

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/libcvariant.so.0

%.pc: %.pc.in
	$(AM_V_GEN)$(SED) \
		-e 's,@includedir\@,$(includedir),g' \
		-e 's,@libdir\@,$(libdir),g' \
		-e 's,@VERSION\@,$(VERSION),g' \
		$< > $@

pkgconfiglib_DATA += \
	src/libcvariant.pc

EXTRA_DIST += \
	src/libcvariant.pc.in

CLEANFILES += \
	src/libcvariant.pc

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

# ------------------------------------------------------------------------------¶
# check "make install" directory tree

install-tree: all
	rm -rf $(abs_srcdir)/install-tree
	$(MAKE) install DESTDIR=$(abs_srcdir)/install-tree
	tree $(abs_srcdir)/install-tree
.PHONY: install-tree
