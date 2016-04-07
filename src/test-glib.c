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
 * Compare CVariant with GLib
 * This marshals random types in both cvariant and glib, and decodes it with
 * the other one to make sure they produce the same results.
 */

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.bus1/c-variant.h"
#include "c-variant-private.h"
#include "generator.h"

#define TEST_VARG_TYPE(_varg, _prepend, _append) ({ \
                const char *__prepend = (_prepend); \
                const char *__append = (_append); \
                const char *__t; \
                size_t __pl = strlen(__prepend); \
                size_t __al = strlen(__append); \
                size_t __tl; \
                char *__s; \
                \
                __t = c_variant_varg_type((_varg), &__tl); \
                __s = malloc(__pl + __tl + __al + 1); \
                assert(__s); \
                memcpy(__s, __prepend, __pl); \
                memcpy(__s + __pl, __t, __tl); \
                memcpy(__s + __pl + __tl, __append, __al); \
                __s[__pl + __tl + __al] = 0; \
                __s; \
        })

static void test_generate(const char *type, CVariant **cvp, GVariant **gvp) {
        GVariantBuilder *builder;
        CVariantVarg varg;
        CVariant *cv;
        GVariant *gv, *gvc;
        uint64_t val_64;
        size_t n;
        char *s;
        int r, c;

        n = strlen(type);

        r = c_variant_new(&cv, type, n);
        assert(r >= 0);

        /* wrap as tuple as GVariantBuilder cannot deal with basic types */
        s = alloca(n + 2);
        s[0] = '(';
        memcpy(s + 1, type, n);
        s[n + 1] = ')';
        s[n + 2] = 0;
        builder = g_variant_builder_new(G_VARIANT_TYPE(s));

        for (c = c_variant_varg_init(&varg, type, strlen(type));
             c;
             c = c_variant_varg_next(&varg)) {
                val_64 = rand();
                val_64 <<= 32;
                val_64 |= rand();

                switch (c) {
                case -1:
                        c_variant_end(cv, NULL);
                        g_variant_builder_close(builder);
                        break;
                case C_VARIANT_VARIANT:
                        c_variant_write(cv, "v", "u", (uint32_t)val_64);
                        g_variant_builder_add(builder, "v",
                                              g_variant_new("u", (uint32_t)val_64));
                        break;
                case C_VARIANT_MAYBE:
                        c_variant_begin(cv, "m");
                        c_variant_varg_enter_bound(&varg, cv, val_64 & 1);

                        s = TEST_VARG_TYPE(&varg, "m", "");
                        g_variant_builder_open(builder, G_VARIANT_TYPE(s));
                        free(s);
                        break;
                case C_VARIANT_ARRAY:
                        c_variant_begin(cv, "a");
                        c_variant_varg_enter_bound(&varg, cv, val_64 & 0xf);

                        s = TEST_VARG_TYPE(&varg, "a", "");
                        g_variant_builder_open(builder, G_VARIANT_TYPE(s));
                        free(s);
                        break;
                case C_VARIANT_TUPLE_OPEN:
                        c_variant_begin(cv, "(");
                        c_variant_varg_enter_unbound(&varg, cv, ')');

                        s = TEST_VARG_TYPE(&varg, "(", ")");
                        g_variant_builder_open(builder, G_VARIANT_TYPE(s));
                        free(s);
                        break;
                case C_VARIANT_PAIR_OPEN:
                        c_variant_begin(cv, "{");
                        c_variant_varg_enter_unbound(&varg, cv, '}');

                        s = TEST_VARG_TYPE(&varg, "{", "}");
                        g_variant_builder_open(builder, G_VARIANT_TYPE(s));
                        free(s);
                        break;
                case C_VARIANT_INT64:
                        c_variant_write(cv, "x", val_64);
                        g_variant_builder_add(builder, "x", val_64);
                        break;
                case C_VARIANT_UINT64:
                        c_variant_write(cv, "t", val_64);
                        g_variant_builder_add(builder, "t", val_64);
                        break;
                case C_VARIANT_DOUBLE:
                        c_variant_write(cv, "d", *(double *)&val_64);
                        g_variant_builder_add(builder, "d", *(double *)&val_64);
                        break;
                case C_VARIANT_INT32:
                        c_variant_write(cv, "i", (uint32_t)val_64);
                        g_variant_builder_add(builder, "i", (uint32_t)val_64);
                        break;
                case C_VARIANT_UINT32:
                        c_variant_write(cv, "u", (uint32_t)val_64);
                        g_variant_builder_add(builder, "u", (uint32_t)val_64);
                        break;
                case C_VARIANT_HANDLE:
                        c_variant_write(cv, "h", (uint32_t)val_64);
                        g_variant_builder_add(builder, "h", (uint32_t)val_64);
                        break;
                case C_VARIANT_INT16:
                        c_variant_write(cv, "n", (int)val_64);
                        g_variant_builder_add(builder, "n", (int)val_64);
                        break;
                case C_VARIANT_UINT16:
                        c_variant_write(cv, "q", (int)val_64);
                        g_variant_builder_add(builder, "q", (int)val_64);
                        break;
                case C_VARIANT_BOOL:
                        c_variant_write(cv, "b", !!val_64);
                        g_variant_builder_add(builder, "b", !!val_64);
                        break;
                case C_VARIANT_BYTE:
                        c_variant_write(cv, "y", (int)val_64);
                        g_variant_builder_add(builder, "y", (int)val_64);
                        break;
                case C_VARIANT_STRING:
                        c_variant_write(cv, "s", "foobar");
                        g_variant_builder_add(builder, "s", "foobar");
                        break;
                case C_VARIANT_PATH:
                        c_variant_write(cv, "o", "/foo/bar");
                        g_variant_builder_add(builder, "o", "/foo/bar");
                        break;
                case C_VARIANT_SIGNATURE:
                        c_variant_write(cv, "g", "bison");
                        g_variant_builder_add(builder, "g", "bison");
                        break;
                default:
                        assert(0);
                        break;
                }
        }

        r = c_variant_seal(cv);
        assert(r >= 0);

        gv = g_variant_builder_end(builder);
        gvc = g_variant_get_child_value(gv, 0);
        g_variant_unref(gv);
        g_variant_builder_unref(builder);

        *cvp = cv;
        *gvp = gvc;
}

static void test_print_cv(CVariant *cv) {
        CVariantVarg varg;
        const char *type, *s;
        uint64_t u64;
        uint32_t u32;
        uint16_t u16;
        uint8_t u8;
        double f64;
        int r, c, nest;
        size_t n;

        nest = 0;
        type = c_variant_peek_type(cv, &n);

        for (c = c_variant_varg_init(&varg, type, n);
             c;
             c = c_variant_varg_next(&varg)) {
                switch (c) {
                case -1:
                        c_variant_exit(cv, NULL);

                        assert(nest-- > 0);
                        printf("%*s}\n", nest * 4, "");
                        break;
                case C_VARIANT_VARIANT:
                        c_variant_enter(cv, "v");
                        type = c_variant_peek_type(cv, &n);
                        c_variant_varg_push(&varg, type, n, -1);

                        printf("%*sv - \"%.*s\" {\n", nest * 4, "", (int)n,  type);
                        ++nest;
                        break;
                case C_VARIANT_MAYBE:
                        c_variant_enter(cv, "m");
                        n = c_variant_peek_count(cv);
                        c_variant_varg_enter_bound(&varg, cv, n);

                        printf("%*sMAYBE - %s {\n", nest * 4, "", n ? "SOMETHING" : "NOTHING");
                        ++nest;
                        break;
                case C_VARIANT_ARRAY:
                        c_variant_enter(cv, "a");
                        n = c_variant_peek_count(cv);
                        c_variant_varg_enter_bound(&varg, cv, n);

                        printf("%*sARRAY - %zu {\n", nest * 4, "", n);
                        ++nest;
                        break;
                case C_VARIANT_TUPLE_OPEN:
                        c_variant_enter(cv, "(");
                        c_variant_varg_enter_unbound(&varg, cv, ')');

                        type = c_variant_peek_type(cv, &n);
                        printf("%*s\"(%.*s)\" {\n", nest * 4, "", (int)n, type);
                        ++nest;
                        break;
                case C_VARIANT_PAIR_OPEN:
                        c_variant_enter(cv, "{");
                        c_variant_varg_enter_unbound(&varg, cv, '}');

                        type = c_variant_peek_type(cv, &n);
                        printf("%*s\"{%.*s}\" {\n", nest * 4, "", (int)n, type);
                        ++nest;
                        break;
                case C_VARIANT_INT64:
                        c_variant_read(cv, "x", &u64);
                        printf("%*sINT64: %"PRId64"\n", nest * 4, "", u64);
                        break;
                case C_VARIANT_UINT64:
                        c_variant_read(cv, "t", &u64);
                        printf("%*sUINT64: %"PRIu64"\n", nest * 4, "", u64);
                        break;
                case C_VARIANT_DOUBLE:
                        c_variant_read(cv, "d", &f64);
                        printf("%*sDOUBLE: %f\n", nest * 4, "", f64);
                        break;
                case C_VARIANT_INT32:
                        c_variant_read(cv, "i", &u32);
                        printf("%*sINT32: %"PRId32"\n", nest * 4, "", u32);
                        break;
                case C_VARIANT_UINT32:
                        c_variant_read(cv, "u", &u32);
                        printf("%*sUINT32: %"PRIu32"\n", nest * 4, "", u32);
                        break;
                case C_VARIANT_HANDLE:
                        c_variant_read(cv, "h", &u32);
                        printf("%*sHANDLE: %"PRIu32"\n", nest * 4, "", u32);
                        break;
                case C_VARIANT_INT16:
                        c_variant_read(cv, "n", &u16);
                        printf("%*sINT16: %"PRId16"\n", nest * 4, "", u16);
                        break;
                case C_VARIANT_UINT16:
                        c_variant_read(cv, "q", &u16);
                        printf("%*sUINT16: %"PRIu16"\n", nest * 4, "", u16);
                        break;
                case C_VARIANT_BOOL:
                        c_variant_read(cv, "b", &u8);
                        printf("%*sBOOL: %s\n", nest * 4, "", u8 ? "TRUE" : "FALSE");
                        break;
                case C_VARIANT_BYTE:
                        c_variant_read(cv, "y", &u8);
                        printf("%*sBYTE: %d\n", nest * 4, "", u8);
                        break;
                case C_VARIANT_STRING:
                        c_variant_read(cv, "s", &s);
                        printf("%*sSTRING: %s\n", nest * 4, "", s);
                        break;
                case C_VARIANT_PATH:
                        c_variant_read(cv, "o", &s);
                        printf("%*sPATH: %s\n", nest * 4, "", s);
                        break;
                case C_VARIANT_SIGNATURE:
                        c_variant_read(cv, "g", &s);
                        printf("%*sSIGNATURE: %s\n", nest * 4, "", s);
                        break;
                default:
                        fprintf(stderr, "ERR: %d\n", c);
                        assert(0);
                        break;
                }

                r = c_variant_return_poison(cv);
                assert(r >= 0);
        }
}

static GBytes *test_cv_get_data_as_bytes(CVariant *cv) {
        const struct iovec *vecs;
        size_t n_vecs, i, pos, size;
        char *data;

        vecs = c_variant_get_vecs(cv, &n_vecs);

        size = 0;
        for (i = 0; i < n_vecs; ++i)
                size += vecs[i].iov_len;

        data = g_malloc(size);

        pos = 0;
        for (i = 0; i < n_vecs; ++i) {
                memcpy(data + pos, vecs[i].iov_base, vecs[i].iov_len);
                pos += vecs[i].iov_len;
        }

        return g_bytes_new_take(data, size);
}

static void test_type(const char *type) {
        CVariant *cv;
        GVariant *gv;
        GBytes *cb, *gb;
        const void *cd, *gd;

        test_generate(type, &cv, &gv);

        cb = test_cv_get_data_as_bytes(cv);
        gb = g_variant_get_data_as_bytes(gv);

        if (g_bytes_compare(cb, gb)) {
                fprintf(stderr, "FAILED: %s\n", type);

                cd = g_bytes_get_data(cb, NULL);
                gd = g_bytes_get_data(gb, NULL);

                fprintf(stderr, "Buffers: %p:%zu %p:%zu\n",
                        cd, g_bytes_get_size(cb),
                        gd, g_bytes_get_size(gb));

                test_print_cv(cv);
                assert(0);
        }

        g_bytes_unref(gb);
        g_bytes_unref(cb);
        g_variant_unref(gv);
        c_variant_free(cv);
}

static void test_basic_set(Generator *gen) {
        uint32_t i, j, n;
        char c, *s;

        printf("Test standard set of 8k variants..\n");

        for (i = 0; i < 8192; ++i) {
                generator_seed_u32(gen, i);
                generator_reset(gen);

                j = 0;
                n = 0;
                s = NULL;

                do {
                        c = generator_step(gen);
                        if (j >= n) {
                                n = n ? (n * 2) : 128;
                                s = realloc(s, n);
                                assert(s);
                        }
                        s[j++] = c;
                } while (c);

                printf("  %u: %s\n", i, s);
                test_type(s);
                free(s);
        }
}

static void test_specific_type(Generator *gen, const char *input) {
        uint32_t j, n;
        char c, *s;

        if (!input[strspn(input, "0123456789")]) {
                generator_seed_str(gen, input, 10);
                generator_reset(gen);

                j = 0;
                n = 0;
                s = NULL;

                do {
                        c = generator_step(gen);
                        if (j >= n) {
                                n = n ? (n * 2) : 128;
                                s = realloc(s, n);
                                assert(s);
                        }
                        s[j++] = c;
                } while (c);
        } else {
                s = strdup(input);
                assert(s);
        }

        printf("Test '%s'..\n", s);

        test_type(s);
        free(s);
}

int main(int argc, char **argv) {
        Generator *gen;
        int r = 0;

        srand(0xdecade); /* make tests reproducible */
        gen = generator_new();

        if (argc == 1) {
                test_basic_set(gen);
        } else if (argc == 2) {
                test_specific_type(gen, argv[1]);
        } else {
                fprintf(stderr, "usage: %s [number/type]\n",
                        program_invocation_short_name);
                r = 77;
        }

        generator_free(gen);
        return r;
}
