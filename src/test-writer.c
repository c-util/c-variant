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
#include "c-variant.h"
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

int main(int argc, char **argv) {
        test_writer_basic();
        return 0;
}
