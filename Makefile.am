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

CLEANFILES = $(BUILT_SOURCES)
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
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
# libcvariant-private

noinst_LTLIBRARIES += \
	libcvariant-private.la

libcvariant_private_la_SOURCES = \
	src/c-variant.h \
	src/c-variant-private.h \
	src/c-variant.c

# ------------------------------------------------------------------------------
# libcvariant

lib_LTLIBRARIES += \
	libcvariant.la

libcvariant_la_SOURCES = \
	$(libcvariant_private_la_SOURCES)

libcvariant_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-info $(LIBCVARIANT_CURRENT):$(LIBCVARIANT_REVISION):$(LIBCVARIANT_AGE) \
	-Wl,--version-script=$(top_srcdir)/src/libcvariant.sym

# ------------------------------------------------------------------------------
# test-api

default_tests += \
	test-api

test_api_SOURCES = \
	src/test-api.c

test_api_LDADD = \
	libcvariant.la # explicitly linked against public library

# ------------------------------------------------------------------------------
# test-reader

default_tests += \
	test-reader

test_reader_SOURCES = \
	src/test-reader.c

test_reader_LDADD = \
	libcvariant-private.la

# ------------------------------------------------------------------------------
# test-signature

default_tests += \
	test-signature

test_signature_SOURCES = \
	src/test-signature.c

test_signature_LDADD = \
	libcvariant-private.la
