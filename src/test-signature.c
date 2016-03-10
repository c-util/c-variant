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
 * Tests for Signature Parser
 * This test verifies the signature parser works correctly. This includes size
 * calculations of fixed types, as well as correct alignment inheritance.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.bus1/c-variant.h"
#include "c-variant-private.h"

static void test_signature_api(void) {
        CVariantType t;
        int r;

        /*
         * Verify that we never read more data from the signature than
         * requested by the caller. Hence, this call should never see the
         * invalid '$' element.
         */

        r = c_variant_signature_next("$foobar", 0, &t);
        assert(r == 0);
        assert(t.alignment == 0);
        assert(t.size == 0);
        assert(t.bound_size == 0);
        assert(t.n_levels == 0);
        assert(t.n_type == 0);

        /*
         * Verify that we never look ahead of a parsed type. That is, parsing
         * the valid element 'b' should return before looking at the invalid
         * element '$' ahead.
         * But if we then strip it (that is, we continue after the parsed
         * element), it should immediately fail but leave 't' untouched.
         */

        r = c_variant_signature_next("b$foobar", 8, &t);
        assert(r == 1);
        assert(t.alignment == 0);
        assert(t.size == 1);
        assert(t.bound_size == 0);
        assert(t.n_levels == 0);
        assert(t.n_type == 1);
        assert(!strncmp(t.type, "b", t.n_type));

        r = c_variant_signature_next(t.type + t.n_type, 8 - t.n_type, &t);
        assert(r < 0);
        assert(r == -EMEDIUMTYPE);
        /* @t must not have been touched! */
        assert(t.alignment == 0);
        assert(t.size == 1);
        assert(t.bound_size == 0);
        assert(t.n_levels == 0);
        assert(t.n_type == 1);
        assert(!strncmp(t.type, "b", t.n_type));

        /*
         * Make sure an inappropriately sized signature will result in an
         * immediate error without the data being looked at.
         * This is not explicitly part of the *ABI*, but reasonable enough to
         * test for.
         */

        r = c_variant_signature_next(NULL, SIZE_MAX, &t);
        assert(r < 0);
        assert(r == -EMSGSIZE);

        /*
         * Make sure API-types are rejected in type-strings. This includes
         * anything that is not a valid element.
         */

        r = c_variant_signature_next("r", 1, &t);
        assert(r < 0);
        assert(r == -EMEDIUMTYPE);

        r = c_variant_signature_next("e", 1, &t);
        assert(r < 0);
        assert(r == -EMEDIUMTYPE);

        r = c_variant_signature_next("?", 1, &t);
        assert(r < 0);
        assert(r == -EMEDIUMTYPE);

        r = c_variant_signature_next("*", 1, &t);
        assert(r < 0);
        assert(r == -EMEDIUMTYPE);
}

static void test_signature_basic(void) {
        CVariantType results[] = {
                {
                        .alignment = 0,
                        .size = 1,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "b",
                },
                {
                        .alignment = 0,
                        .size = 1,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "y",
                },
                {
                        .alignment = 1,
                        .size = 2,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "n",
                },
                {
                        .alignment = 1,
                        .size = 2,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "q",
                },
                {
                        .alignment = 2,
                        .size = 4,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "i",
                },
                {
                        .alignment = 2,
                        .size = 4,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "u",
                },
                {
                        .alignment = 3,
                        .size = 8,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "x",
                },
                {
                        .alignment = 3,
                        .size = 8,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "t",
                },
                {
                        .alignment = 2,
                        .size = 4,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "h",
                },
                {
                        .alignment = 3,
                        .size = 8,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "d",
                },
                {
                        .alignment = 0,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "s",
                },
                {
                        .alignment = 0,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "o",
                },
                {
                        .alignment = 0,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "g",
                },
                {
                        .alignment = 3,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "v",
                },
        };
        const char *pos, *signature = "bynqiuxthdsogv";
        CVariantType t;
        size_t i, len;
        int r;

        /*
         * @results contains an array of basic types. This loop parses each
         * type and verifies the result is as expected.
         */

        pos = signature;
        len = strlen(pos);

        for (i = 0; i < sizeof(results) / sizeof(*results); ++i) {
                r = c_variant_signature_next(pos, len, &t);
                assert(r == 1);
                assert(t.n_type <= len);
                assert(t.alignment == results[i].alignment);
                assert(t.size == results[i].size);
                assert(t.bound_size == results[i].bound_size);
                assert(t.n_levels == results[i].n_levels);
                assert(t.n_type == results[i].n_type);
                assert(!strncmp(t.type, results[i].type, t.n_type));

                pos += t.n_type;
                len -= t.n_type;
        }

        assert(len == 0);

        r = c_variant_signature_next(pos, len, &t);
        assert(r == 0);
}

static void test_signature_containers(void) {
        CVariantType results[] = {
                {
                        .alignment = 0,
                        .size = 1,
                        .bound_size = 0,
                        .n_levels = 0,
                        .n_type = 1,
                        .type = "b",
                },
                {
                        .alignment = 0,
                        .size = 0,
                        .bound_size = 1,
                        .n_levels = 1,
                        .n_type = 2,
                        .type = "mb",
                },
                {
                        .alignment = 3,
                        .size = 16,
                        .bound_size = 0,
                        .n_levels = 1,
                        .n_type = 4,
                        .type = "(ty)",
                },
                {
                        .alignment = 0,
                        .size = 2,
                        .bound_size = 0,
                        .n_levels = 1,
                        .n_type = 4,
                        .type = "(yy)",
                },
                {
                        .alignment = 3,
                        .size = 24,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "(y(ty))",
                },
                {
                        .alignment = 3,
                        .size = 24,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "{y(ty)}",
                },
                {
                        .alignment = 0,
                        .size = 1,
                        .bound_size = 0,
                        .n_levels = 1,
                        .n_type = 2,
                        .type = "()",
                },
                {
                        .alignment = 2,
                        .size = 8,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 5,
                        .type = "{u()}",
                },
                {
                        .alignment = 3,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 1,
                        .n_type = 4,
                        .type = "{uv}",
                },
                {
                        .alignment = 3,
                        .size = 16,
                        .bound_size = 0,
                        .n_levels = 1,
                        .n_type = 4,
                        .type = "{ut}",
                },
                {
                        .alignment = 3,
                        .size = 16,
                        .bound_size = 0,
                        .n_levels = 1,
                        .n_type = 8,
                        .type = "(uyyyyt)",
                },
                {
                        .alignment = 2,
                        .size = 32,
                        .bound_size = 0,
                        .n_levels = 4,
                        .n_type = 16,
                        .type = "(u(u(u(uu)u)u)u)",
                },
                {
                        .alignment = 2,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 5,
                        .n_type = 16,
                        .type = "(u(u(u(mu)u)u)u)",
                },
                {
                        .alignment = 0,
                        .size = 3,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "(y(yy))",
                },
                {
                        .alignment = 3,
                        .size = 24,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "(y(tt))",
                },
                {
                        .alignment = 2,
                        .size = 12,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "(y(uu))",
                },
                {
                        .alignment = 3,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "(y(vv))",
                },
                {
                        .alignment = 0,
                        .size = 0,
                        .bound_size = 3,
                        .n_levels = 3,
                        .n_type = 8,
                        .type = "m(y(yy))",
                },
                {
                        .alignment = 2,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 3,
                        .n_type = 6,
                        .type = "a{ums}",
                },
                {
                        .alignment = 2,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 3,
                        .n_type = 4,
                        .type = "aaau",
                },
                {
                        .alignment = 2,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 4,
                        .n_type = 5,
                        .type = "mamau",
                },
                {
                        .alignment = 0,
                        .size = 0,
                        .bound_size = 0,
                        .n_levels = 4,
                        .n_type = 5,
                        .type = "aaa()",
                },
                {
                        .alignment = 3,
                        .size = 0,
                        .bound_size = 16,
                        .n_levels = 2,
                        .n_type = 7,
                        .type = "a(tunb)",
                },
                {
                        .alignment = 3,
                        .size = 0,
                        .bound_size = 16,
                        .n_levels = 3,
                        .n_type = 7,
                        .type = "a(t(u))",
                },
        };
        char *signature, *pos;
        CVariantType t;
        size_t i, len;
        int r;

        /*
         * This concatenates all types in the @results array and then parses
         * them one by one. All must be parsed correctly.
         */

        len = 0;
        for (i = 0; i < sizeof(results) / sizeof(*results); ++i)
                len += results[i].n_type;

        signature = malloc(len + 1);
        pos = signature;
        for (i = 0; i < sizeof(results) / sizeof(*results); ++i)
                pos = mempcpy(pos, results[i].type, results[i].n_type);

        pos = signature;
        for (i = 0; i < sizeof(results) / sizeof(*results); ++i) {
                r = c_variant_signature_next(pos, len, &t);
                assert(r == 1);
                assert(t.n_type <= len);
                assert(t.alignment == results[i].alignment);
                assert(t.size == results[i].size);
                assert(t.bound_size == results[i].bound_size);
                assert(t.n_levels == results[i].n_levels);
                assert(t.n_type == results[i].n_type);
                assert(!strncmp(t.type, results[i].type, t.n_type));

                pos += t.n_type;
                len -= t.n_type;
        }

        assert(len == 0);

        r = c_variant_signature_next(pos, len, &t);
        assert(r == 0);
}

static void test_signature_invalid(void) {
        const char *signatures[] = {
                "A", /* invalid element */
                "$", /* invalid element */
                "{}", /* invalid pair */
                "{)", /* non-matching brackets */
                "(}", /* non-matching brackets */
                "{()y}", /* invalid pair */
                "{yyy}", /* invalid pair */
                "(yy{))", /* non-matching brackets */
                "(yy{}}", /* invalid pair */
                "(yy{)}", /* non-matching brackets */
                "(", /* unclosed container */
                ")", /* closing wrong container */
                "((", /* unclosed container */
                ")(", /* closing wrong container */
                "a", /* unclosed container */
                "m", /* unclosed container */
                "mm", /* unclosed container */
                "mama", /* unclosed container */
                "{mau}", /* invalid pair */
                "{vu}", /* invalid pair*/
                "(uu(u())uu{vu}uu)", /* invalid pair */
                "(uu(u())uu(vu}uu)", /* non-matching brackets */
                "(uu(u())uu{uu)uu)", /* non-matching brackets */
                "(uu(u())uuuuuuuu}", /* non-matching brackets */
        };
        CVariantType t;
        size_t i;
        int r;

        /*
         * This parses the signatures provided by @signatures one by one, each
         * of them must be rejected.
         */

        for (i = 0; i < sizeof(signatures) / sizeof(*signatures); ++i) {
                r = c_variant_signature_next(signatures[i], strlen(signatures[i]), &t);
                assert(r < 0);
                assert(r == -EMEDIUMTYPE || r == -ELOOP);
        }
}

int main(int argc, char **argv) {
        test_signature_api();
        test_signature_basic();
        test_signature_containers();
        test_signature_invalid();
        return 0;
}
