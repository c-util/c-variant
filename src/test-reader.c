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
 * Tests for readers
 * This test contains basic verifications for variant readers. It mostly
 * includes static tests with known corner cases.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c-variant.h"
#include "c-variant-private.h"

static void test_reader_basic(void) {
        const char *type;
        unsigned int u1;
        CVariant *cv;
        int r;

        /* simple 'u' type */
        type = "u";
        r = c_variant_new_from_buffer(&cv, type, strlen(type),
                                      "\xff\x00\xff\x00", 4);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "u", &u1);
        assert(r >= 0);
        assert(u1 == 0x00ff00ff);

        c_variant_rewind(cv);

        r = c_variant_read(cv, NULL);
        assert(r >= 0);
        r = c_variant_read(cv, "");
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "u", &u1);
        assert(r >= 0);
        assert(u1 == 0x00ff00ff);

        cv = c_variant_free(cv);

        /* compound '(u)' type */
        type = "(u)";
        r = c_variant_new_from_buffer(&cv, type, strlen(type),
                                      "\xff\x00\xff\x00", 4);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "u", &u1);
        assert(r < 0);

        r = c_variant_read(cv, "(u)", &u1);
        assert(r >= 0);
        assert(u1 == 0x00ff00ff);

        cv = c_variant_free(cv);

        /* trivial array 'au' */
        type = "au";
        r = c_variant_new_from_buffer(&cv, type, strlen(type),
                                      "\xff\x00\xff\x00", 4);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "u", &u1);
        assert(r < 0);

        r = c_variant_read(cv, "(u)", &u1);
        assert(r < 0);

        r = c_variant_read(cv, "au", 1, &u1);
        assert(r >= 0);
        assert(u1 == 0x00ff00ff);

        cv = c_variant_free(cv);

        /* trivial maybe 'mu' */
        type = "mu";
        r = c_variant_new_from_buffer(&cv, type, strlen(type),
                                      "\xff\x00\xff\x00", 4);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "u", &u1);
        assert(r < 0);

        r = c_variant_read(cv, "mu", true, &u1);
        assert(r >= 0);
        assert(u1 == 0x00ff00ff);

        cv = c_variant_free(cv);

        /* trivial variant 'v', 'u' */
        type = "v";
        r = c_variant_new_from_buffer(&cv, type, strlen(type),
                                      "\xff\x00\xff\x00\0u", 6);
        assert(r >= 0);

        u1 = 0;
        r = c_variant_read(cv, "v", "u", &u1);
        assert(r >= 0);
        assert(u1 == 0x00ff00ff);

        cv = c_variant_free(cv);
}

static void test_reader_compound(void) {
        const char data[] = {
                "\xff\xff\x00\x00"
                "\x01\x00\x00\x00"
                "\x02\x00\x00\x00"
                "\x03\x00\x00\x00"
                "\x04\x00\x00\x00"
                "foo\0"
                "\0"
                "\0\0\0"
                "\xff\xff\xff\xff"
                "\x19"
                "\x14"
        };
        const char *type = "(uaum(s)u)";
        unsigned int u1, u2, u3, u4, u5, u6;
        const char *s1;
        CVariant *cv;
        int r;

        /* allocate variant and read each entry sequentially */

        r = c_variant_new_from_buffer(&cv, type, strlen(type), data, sizeof(data) - 1);
        assert(r >= 0);

        r = c_variant_enter(cv, "(");
        assert(r >= 0);

        r = c_variant_read(cv, "u", &u1);
        assert(r >= 0);
        assert(u1 == 0xffff);

        r = c_variant_read(cv, "au", 4, &u2, &u3, &u4, &u5);
        assert(r >= 0);
        assert(u2 == 1);
        assert(u3 == 2);
        assert(u4 == 3);
        assert(u5 == 4);

        r = c_variant_read(cv, "m(s)", true, &s1);
        assert(r >= 0);
        assert(!strcmp(s1, "foo"));

        r = c_variant_read(cv, "u", &u6);
        assert(r >= 0);
        assert(u6 == 0xffffffffU);

        r = c_variant_exit(cv, ")");
        assert(r >= 0);

        /* rewind and then read everything again in one batch */
        c_variant_rewind(cv);

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
        test_reader_basic();
        test_reader_compound();
        return 0;
}
