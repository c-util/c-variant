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
 * Test GVariant Generator
 * This contains basic tests for the GVariant generator provided for the test
 * suite via the "Generator" type.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.bus1/c-variant.h"
#include "c-variant-private.h"
#include "generator.h"

static void n_to_gv(Generator *gen, const char *s) {
        char c;

        generator_seed_str(gen, s, 10);

        while ((c = generator_step(gen)))
                printf("%c", c);
}

static void gv_to_n(Generator *gen, const char *s) {
        do {
                generator_feed(gen, *s);
        } while (*s++);

        generator_print(gen, stdout, 10);
}

int main(int argc, char **argv) {
        Generator *gen;

        gen = generator_new();

        if (argc < 2) {
                /* XXX: run standard tests */
        } else if (argc == 2) {
                if (!argv[1][strspn(argv[1], "0123456789")])
                        n_to_gv(gen, argv[1]);
                else
                        gv_to_n(gen, argv[1]);
                printf("\n");
        } else if (argc == 3 && !strcmp(argv[1], "fold")) {
                gv_to_n(gen, argv[2]);
                printf("\n");
        } else if (argc == 3 && !strcmp(argv[1], "unfold")) {
                n_to_gv(gen, argv[2]);
                printf("\n");
        } else {
                fprintf(stderr, "usage: %s [fold|unfold] [<number>]\n",
                        program_invocation_short_name);
        }

        gen = generator_free(gen);

        return 0;
}
