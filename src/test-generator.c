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
#include "c-variant.h"
#include "c-variant-private.h"
#include "generator.h"

int main(int argc, char **argv) {
        Generator *gen;
        char c;

        gen = generator_new();

        if (argc < 2) {
                /* XXX: run standard tests */
        } else if (argc == 2) {
                generator_seed_str(gen, argv[1], 10);
                while ((c = generator_step(gen)))
                        printf("%c", c);
                printf("\n");
        } else {
                fprintf(stderr, "usage: %s [<number>]\n",
                        program_invocation_short_name);
        }

        gen = generator_free(gen);

        return 0;
}
