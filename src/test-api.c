/***
  This file is part of bus1. See COPYING for details.

  bus1 is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  bus1 is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with bus1; If not, see <http://www.gnu.org/licenses/>.
***/

/*
 * Tests for Public API
 * This test, unlikely the others, is linked against the real, distributed,
 * shared library. Its sole purpose is to test for symbol availability.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.bus1/c-variant.h"

static void test_api_constants(void) {
        assert(C_VARIANT_MAX_LEVEL >= (1 << 8) - 1);
        assert(C_VARIANT_MAX_SIGNATURE >= (1 << 16) - 1);
        assert(C_VARIANT_MAX_VARG >= (1 << 4) - 1);
}

static void test_api_symbols(void) {
        const char *type;
        CVariant *cv;
        va_list args;
        size_t n;
        int r;

        /* c_variant_new(), c_variant_new_from_vecs(), c_variant_free() */

        r = c_variant_new(&cv, "()", 2);
        assert(r >= 0);

        cv = c_variant_free(cv);
        assert(!cv);

        r = c_variant_new_from_vecs(&cv, "()", 2, NULL, 0);
        assert(r >= 0);

        /* c_variant_{is_sealed,return_poison,get_vecs}() */

        r = c_variant_is_sealed(cv);
        assert(!!r);

        r = c_variant_return_poison(cv);
        assert(!r);

        c_variant_get_vecs(cv, &n);

        /* c_variant_{peek_count,peek_type}() */

        n = c_variant_peek_count(cv);

        type = c_variant_peek_type(cv, &n);
        assert(!!type);

        /* c_variant_{enter,exit,readv,rewind}() */

        r = c_variant_enter(cv, "(");
        assert(r >= 0);

        r = c_variant_exit(cv, ")");
        assert(r >= 0);

        c_variant_rewind(cv);

        r = c_variant_readv(cv, "()", args);
        assert(r >= 0);

        /* cleanup */

        cv = c_variant_free(cv);
        assert(!cv);

        /* c_variant_{beginv,end,writev,seal}() */

        r = c_variant_new(&cv, "()", 2);
        assert(r >= 0);

        r = c_variant_beginv(cv, "(", args);
        assert(r >= 0);

        r = c_variant_writev(cv, "", args);
        assert(r >= 0);

        r = c_variant_end(cv, ")");
        assert(r >= 0);

        /* cleanup */

        cv = c_variant_free(cv);
        assert(!cv);
}

int main(int argc, char **argv) {
        test_api_constants();
        test_api_symbols();
        return 0;
}
