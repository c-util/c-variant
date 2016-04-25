#pragma once

/***
  This file is part of c-variant. See COPYING for details.

  c-variant is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  c-variant is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with c-variant; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Generator Generator;

Generator *generator_new(void);
Generator *generator_free(Generator *gen);
void generator_reset(Generator *gen);

void generator_seed_u32(Generator *gen, uint32_t seed);
int generator_seed_str(Generator *gen, const char *str, int base);
char generator_step(Generator *gen);

int generator_feed(Generator *gen, char c);
void generator_print(Generator *gen, FILE *f, int base);

#ifdef __cplusplus
}
#endif
