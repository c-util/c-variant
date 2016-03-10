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
 * Tests for writers
 * This test contains basic verifications for variant writers. It mostly
 * includes static tests with known corner cases.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.bus1/c-variant.h"
#include "c-variant-private.h"

static void test_writer_basic(void) {
        const char *type;
        unsigned int u1;
        CVariant *cv;
        int r;

        /* simple 'u' type */
        type = "u";
        r = c_variant_new(&cv, type, strlen(type));
        assert(r >= 0);

        u1 = 0xf0f0;
        r = c_variant_write(cv, "u", u1);
        assert(r >= 0);

        r = c_variant_seal(cv);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "u", &u1);
        assert(r >= 0);
        assert(u1 == 0xf0f0);

        cv = c_variant_free(cv);

        /* compound '(u)' type */
        type = "(u)";
        r = c_variant_new(&cv, type, strlen(type));
        assert(r >= 0);

        u1 = 0xf0f0;
        r = c_variant_write(cv, "(u)", u1);
        assert(r >= 0);

        r = c_variant_seal(cv);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "(u)", &u1);
        assert(r >= 0);
        assert(u1 == 0xf0f0);

        cv = c_variant_free(cv);

        /* array 'au' type */
        type = "au";
        r = c_variant_new(&cv, type, strlen(type));
        assert(r >= 0);

        u1 = 0xf0f0;
        r = c_variant_write(cv, "au", 1, u1);
        assert(r >= 0);

        r = c_variant_seal(cv);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "au", 1, &u1);
        assert(r >= 0);
        assert(u1 == 0xf0f0);

        cv = c_variant_free(cv);

        /* maybe 'mu' type */
        type = "mu";
        r = c_variant_new(&cv, type, strlen(type));
        assert(r >= 0);

        u1 = 0xf0f0;
        r = c_variant_write(cv, "mu", true, u1);
        assert(r >= 0);

        r = c_variant_seal(cv);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "mu", true, &u1);
        assert(r >= 0);
        assert(u1 == 0xf0f0);

        cv = c_variant_free(cv);

        /* variant 'v', 'u' */
        type = "v";
        r = c_variant_new(&cv, type, strlen(type));
        assert(r >= 0);

        u1 = 0xf0f0;
        r = c_variant_write(cv, "v", "u", u1);
        assert(r >= 0);

        r = c_variant_seal(cv);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "v", "u", &u1);
        assert(r >= 0);
        assert(u1 == 0xf0f0);

        cv = c_variant_free(cv);
}

static void test_writer_compound(void) {
        const char *type = "(uaum(s)u)";
        unsigned int u1, u2, u3, u4, u5, u6;
        const char *s1;
        CVariant *cv;
        int r;

        /* allocate variant and write each entry sequentially */

        r = c_variant_new(&cv, type, strlen(type));
        assert(r >= 0);

        r = c_variant_begin(cv, "(");
        assert(r >= 0);

        r = c_variant_write(cv, "u", 0xffff);
        assert(r >= 0);

        r = c_variant_write(cv, "au", 4, 1, 2, 3, 4);
        assert(r >= 0);

        r = c_variant_write(cv, "m(s)", true, "foo");
        assert(r >= 0);

        r = c_variant_write(cv, "u", 0xffffffffU);
        assert(r >= 0);

        r = c_variant_end(cv, ")");
        assert(r >= 0);

        /* seal and verify */
        r = c_variant_seal(cv);
        assert(r >= 0);

        r = c_variant_read(cv, "(uaum(s)u)",
                           &u1,
                           4, &u2, &u3, &u4, &u5,
                           true, &s1,
                           &u6);
        assert(r >= 0);
        assert(u1 == 0xffff);
        assert(u2 == 1);
        assert(u3 == 2);
        assert(u4 == 3);
        assert(u5 == 4);
        assert(!strcmp(s1, "foo"));
        assert(u6 == 0xffffffffU);

        cv = c_variant_free(cv);
        assert(!cv);
}

int main(int argc, char **argv) {
        test_writer_basic();
        test_writer_compound();
        return 0;
}
