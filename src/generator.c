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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* s*$&! gmp depends on include order; order after std-headers */
#include <gmp.h>
#include "generator.h"

typedef struct GeneratorState GeneratorState;

enum {
        GENERATOR_BASIC_b,
        GENERATOR_BASIC_y,
        GENERATOR_BASIC_n,
        GENERATOR_BASIC_q,
        GENERATOR_BASIC_i,
        GENERATOR_BASIC_u,
        GENERATOR_BASIC_x,
        GENERATOR_BASIC_t,
        GENERATOR_BASIC_h,
        GENERATOR_BASIC_d,
        GENERATOR_BASIC_s,
        GENERATOR_BASIC_o,
        GENERATOR_BASIC_g,
        _GENERATOR_BASIC_N,
};

enum {
        GENERATOR_COMPOUND_m,
        GENERATOR_COMPOUND_a,
        GENERATOR_COMPOUND_r,
        GENERATOR_COMPOUND_e,
        _GENERATOR_COMPOUND_N,
};

struct GeneratorState {
        GeneratorState *parent;
        unsigned int rule;
        mpz_t seed;
};

struct Generator {
        GeneratorState *tip;
        GeneratorState *unused;
        mpz_t seed;
        mpz_t root;
        mpz_t root_squared;
        mpz_t index;
};

/*
 * (Inverse) Pair
 * ==============
 *
 * These functions implement a 'pair-function' and its inverse. It is quite
 * similar to the '(inverse) Cantor pairing function', but way easier to
 * calculate. The Cantor-pairing-function would have worked as well, though.
 */

static void generator_pi(Generator *gen, mpz_t seed, mpz_t pi1, mpz_t pi2) {
        int res;

        /*
         * Pairing function that takes @pi1 and @pi2 and returns the pair as
         * @seed. We use @gen as temporary variables, to avoid dynamic
         * allocation on each call.
         */

        /* CAREFUL: pi1/pi2/seed may *overlap*! */

        res = mpz_cmp(pi1, pi2);
        if (res < 0) {
                mpz_pow_ui(gen->index, pi2, 2);
                mpz_add(seed, gen->index, pi1);
        } else {
                mpz_pow_ui(gen->index, pi1, 2);
                mpz_add(gen->index, gen->index, pi1);
                mpz_add(seed, gen->index, pi2);
        }
}

static void generator_inverse_pi(Generator *gen, mpz_t pi1, mpz_t pi2, mpz_t seed) {
        int res;

        /*
         * Inverse pairing function that takes @seed as input and returns the
         * inverse-pair as @pi1 and @pi2. We use storage on @gen as temporary
         * variables, to avoid dynamic allocation on each call.
         */

        /* CAREFUL: pi1/pi2/seed may *overlap*! */

        mpz_sqrt(gen->root, seed);
        mpz_pow_ui(gen->root_squared, gen->root, 2);
        mpz_sub(gen->index, seed, gen->root_squared);

        res = mpz_cmp(gen->index, gen->root);
        if (res < 0) {
                mpz_sub(pi1, seed, gen->root_squared);
                mpz_set(pi2, gen->root);
        } else {
                mpz_sub(gen->index, seed, gen->root_squared);
                mpz_sub(pi2, gen->index, gen->root);
                mpz_set(pi1, gen->root);
        }
}

/*
 * State
 * =====
 *
 * To implement a context-free grammar, we chose a deterministic push-down
 * automata (PDA). It is very simple: We have a push-down stack of
 * GeneratorState objects. The top object is accessible via gen->tip. You can
 * push and pop states, according to your needs. We cache old states for later
 * reuse, which avoids reallocation of GnuMP objects.
 */

static GeneratorState *generator_state_new(void) {
        GeneratorState *state;

        state = calloc(1, sizeof(*state));
        assert(state);

        mpz_init(state->seed);

        return state;
}

static GeneratorState *generator_state_free(GeneratorState *state) {
        if (!state)
                return NULL;

        mpz_clear(state->seed);
        free(state);

        return NULL;
}

static GeneratorState *generator_push(Generator *gen) {
        GeneratorState *state;

        if (gen->unused) {
                state = gen->unused;
                gen->unused = state->parent;
        } else {
                state = generator_state_new();
        }

        state->parent = gen->tip;
        gen->tip = state;
        return state;
}

static GeneratorState *generator_pop(Generator *gen) {
        GeneratorState *state;

        if (gen->tip) {
                state = gen->tip;
                gen->tip = state->parent;
                state->parent = gen->unused;
                gen->unused = state;
        }

        return gen->tip;
}

/*
 * Rules
 * =====
 *
 * These rules implement a context-free grammar that represents the GVariant
 * type language. The rules are as follows:
 *   (terminals are lower-case, non-terminals are upper-case)
 *
 *     TYPE ::= basic
 *              | 'v'
 *              | '(' ')'
 *              | 'm' TYPE
 *              | 'a' TYPE
 *              | '(' TUPLE ')'
 *              | '{' PAIR '}'
 *     TUPLE ::= TYPE | TYPE TUPLE
 *     PAIR ::= basic TYPE
 *
 *     basic ::= integer | float | string
 *     integer ::= 'b' | 'y' | 'n' | 'q' | 'i' | 'u' | 'x' | 't' | 'h'
 *     float ::= 'd'
 *     string ::= 's' | 'o' | 'g'
 *
 * Rather than implementing a parser, we implement a generator here. On each
 * rule, we decide based on its seed which possible evaluation to take. We then
 * modify the seed based on the decision we placed and pass the seed to the
 * sub-rule that we chose.
 *
 * To guarantee that we get a proper bijection between the natural numbers (our
 * seed), and the GVariant type-space, we must make sure to never throw away
 * information from the seed, but also exactly take away the information that
 * was required for our decision. In most cases this is straightforward, but
 * some parts (eg., TUPLE evaluation) need to split the seed. We use an inverse
 * pair function for that (a variation of Cantor's pair-function). You are
 * recommended to read up 'diagonalisation' and '(inverse) pair-functions'
 * before trying to understand the implementation.
 */

enum {
        GENERATOR_RULE_DONE,
        GENERATOR_RULE_TYPE,
        GENERATOR_RULE_TUPLE,
        GENERATOR_RULE_TUPLE_CLOSE,
        GENERATOR_RULE_PAIR,
        GENERATOR_RULE_PAIR_CLOSE,
        _GENERATOR_RULE_N,
};

static char generator_map_basic(unsigned long val) {
        switch (val) {
        case GENERATOR_BASIC_b:
                return 'b';
        case GENERATOR_BASIC_y:
                return 'y';
        case GENERATOR_BASIC_n:
                return 'n';
        case GENERATOR_BASIC_q:
                return 'q';
        case GENERATOR_BASIC_i:
                return 'i';
        case GENERATOR_BASIC_u:
                return 'u';
        case GENERATOR_BASIC_x:
                return 'x';
        case GENERATOR_BASIC_t:
                return 't';
        case GENERATOR_BASIC_h:
                return 'h';
        case GENERATOR_BASIC_d:
                return 'd';
        case GENERATOR_BASIC_s:
                return 's';
        case GENERATOR_BASIC_o:
                return 'o';
        case GENERATOR_BASIC_g:
                return 'g';
        default:
                assert(0);
                return 0;
        }
}

static int generator_rule_TYPE(Generator *gen) {
        GeneratorState *next, *state = gen->tip;
        unsigned long val;
        int res;

        /*
         * TYPE ::= basic
         *          | 'v'
         *          | '(' ')'
         *          | 'm' TYPE
         *          | 'a' TYPE
         *          | '(' TUPLE ')'
         *          | '{' PAIR '}'
         */

        res = mpz_cmp_ui(state->seed, _GENERATOR_BASIC_N + 2);
        if (res < 0) {
                /*
                 * TYPE ::= basic | 'v' | '(' ')'
                 */
                val = mpz_get_ui(state->seed);
                switch (val) {
                case _GENERATOR_BASIC_N + 0:
                        /*
                         * TYPE ::= 'v'
                         */
                        generator_pop(gen);
                        return 'v';
                case _GENERATOR_BASIC_N + 1:
                        /*
                         * TYPE ::= '(' ')'
                         */
                        state->rule = GENERATOR_RULE_TUPLE_CLOSE;
                        return '(';
                default:
                        /*
                         * TYPE ::= basic
                         */
                        generator_pop(gen);
                        return generator_map_basic(val);
                }
        } else {
                /*
                 * TYPE ::= 'm' TYPE | 'a' TYPE | '(' TUPLE ')' | '{' PAIR '}'
                 */
                mpz_sub_ui(state->seed, state->seed, _GENERATOR_BASIC_N + 2);
                val = mpz_fdiv_q_ui(state->seed, state->seed, _GENERATOR_COMPOUND_N);
                switch (val) {
                case GENERATOR_COMPOUND_m:
                        /*
                         * TYPE ::= 'm' TYPE
                         */
                        state->rule = GENERATOR_RULE_TYPE;
                        return 'm';
                case GENERATOR_COMPOUND_a:
                        /*
                         * TYPE ::= 'a' TYPE
                         */
                        state->rule = GENERATOR_RULE_TYPE;
                        return 'a';
                case GENERATOR_COMPOUND_r:
                        /*
                         * TYPE ::= '(' TUPLE ')'
                         */
                        state->rule = GENERATOR_RULE_TUPLE_CLOSE;
                        next = generator_push(gen);
                        next->rule = GENERATOR_RULE_TUPLE;
                        mpz_set(next->seed, state->seed);
                        return '(';
                case GENERATOR_COMPOUND_e:
                        /*
                         * TYPE ::= '{' PAIR '}'
                         */
                        state->rule = GENERATOR_RULE_PAIR_CLOSE;
                        next = generator_push(gen);
                        next->rule = GENERATOR_RULE_PAIR;
                        mpz_set(next->seed, state->seed);
                        return '{';
                default:
                        assert(0);
                        return 0;
                }
        }
}

static char generator_rule_TUPLE(Generator *gen) {
        GeneratorState *next, *state = gen->tip;
        unsigned long val;

        /*
         * TUPLE ::= TYPE | TYPE TUPLE
         */

        val = mpz_fdiv_q_ui(state->seed, state->seed, 2);
        switch (val) {
        case 0:
                state->rule = GENERATOR_RULE_TYPE;
                return generator_rule_TYPE(gen);
        case 1:
                next = generator_push(gen);
                next->rule = GENERATOR_RULE_TYPE;
                generator_inverse_pi(gen, state->seed, next->seed, state->seed);
                return generator_rule_TYPE(gen);
        default:
                assert(0);
                return 0;
        }
}

static char generator_rule_TUPLE_CLOSE(Generator *gen) {
        generator_pop(gen);
        return ')';
}

static char generator_rule_PAIR(Generator *gen) {
        GeneratorState *state = gen->tip;
        unsigned long val;

        /*
         * PAIR ::= basic TYPE
         */

        val = mpz_fdiv_q_ui(state->seed, state->seed, _GENERATOR_BASIC_N);
        state->rule = GENERATOR_RULE_TYPE;
        return generator_map_basic(val);
}

static char generator_rule_PAIR_CLOSE(Generator *gen) {
        generator_pop(gen);
        return '}';
}

/*
 * Parser
 * ======
 *
 * This parser implements the inverse operation of the generator-rules defined
 * above. It simply inverts each step that the generator does, to allow callers
 * to turn a given gvariant into its corresponding seed.
 *
 * The grammar and names are exactly the same for the inverse operation.
 */

enum {
        GENERATOR_PARSER_DONE,
        GENERATOR_PARSER_FAIL,
        GENERATOR_PARSER_TYPE,
        GENERATOR_PARSER_MAYBE,
        GENERATOR_PARSER_ARRAY,
        GENERATOR_PARSER_TUPLE,
        GENERATOR_PARSER_TUPLE_CLOSE,
        GENERATOR_PARSER_PAIR,
        GENERATOR_PARSER_PAIR_CLOSE,
        _GENERATOR_PARSER_N,
};

static unsigned long generator_unmap_basic(char c) {
        switch (c) {
        case 'b':
                return GENERATOR_BASIC_b;
        case 'y':
                return GENERATOR_BASIC_y;
        case 'n':
                return GENERATOR_BASIC_n;
        case 'q':
                return GENERATOR_BASIC_q;
        case 'i':
                return GENERATOR_BASIC_i;
        case 'u':
                return GENERATOR_BASIC_u;
        case 'x':
                return GENERATOR_BASIC_x;
        case 't':
                return GENERATOR_BASIC_t;
        case 'h':
                return GENERATOR_BASIC_h;
        case 'd':
                return GENERATOR_BASIC_d;
        case 's':
                return GENERATOR_BASIC_s;
        case 'o':
                return GENERATOR_BASIC_o;
        case 'g':
                return GENERATOR_BASIC_g;
        default:
                return _GENERATOR_BASIC_N;
        }
}

static void generator_fold_MAYBE(Generator *gen) {
        GeneratorState *next, *state = gen->tip;

        mpz_mul_ui(state->seed, state->seed, _GENERATOR_COMPOUND_N);
        mpz_add_ui(state->seed, state->seed, GENERATOR_COMPOUND_m);
        mpz_add_ui(state->seed, state->seed, _GENERATOR_BASIC_N + 2);

        next = generator_pop(gen);
        mpz_set(next->seed, state->seed);
}

static void generator_fold_ARRAY(Generator *gen) {
        GeneratorState *next, *state = gen->tip;

        mpz_mul_ui(state->seed, state->seed, _GENERATOR_COMPOUND_N);
        mpz_add_ui(state->seed, state->seed, GENERATOR_COMPOUND_a);
        mpz_add_ui(state->seed, state->seed, _GENERATOR_BASIC_N + 2);

        next = generator_pop(gen);
        mpz_set(next->seed, state->seed);
}

static int generator_fold(Generator *gen) {
        GeneratorState *state, *next;

        for (;;) {
                state = gen->tip;
                switch (state->rule) {
                case GENERATOR_PARSER_MAYBE:
                        generator_fold_MAYBE(gen);
                        break;
                case GENERATOR_PARSER_ARRAY:
                        generator_fold_ARRAY(gen);
                        break;
                case GENERATOR_PARSER_TUPLE_CLOSE:
                        next = generator_pop(gen);
                        mpz_set(next->seed, state->seed);
                        break;
                case GENERATOR_PARSER_TUPLE:
                        next = generator_push(gen);
                        next->rule = GENERATOR_PARSER_TUPLE;
                        /* fallthrough */
                default:
                        return 0;
                }
        }
}

static int generator_parser_TYPE(Generator *gen, char c) {
        GeneratorState *next, *state = gen->tip;
        unsigned long val;

        val = generator_unmap_basic(c);
        if (val < _GENERATOR_BASIC_N) {
                next = generator_pop(gen);
                mpz_set_ui(next->seed, val);
                return generator_fold(gen);
        } else if (c == 'v') {
                next = generator_pop(gen);
                mpz_set_ui(next->seed, _GENERATOR_BASIC_N);
                return generator_fold(gen);
        } else {
                switch (c) {
                case 'm':
                        state->rule = GENERATOR_PARSER_MAYBE;
                        next = generator_push(gen);
                        next->rule = GENERATOR_PARSER_TYPE;
                        break;
                case 'a':
                        state->rule = GENERATOR_PARSER_ARRAY;
                        next = generator_push(gen);
                        next->rule = GENERATOR_PARSER_TYPE;
                        break;
                case '(':
                        state->rule = GENERATOR_PARSER_TUPLE_CLOSE;
                        next = generator_push(gen);
                        next->rule = GENERATOR_PARSER_TUPLE;
                        return 0;
                case '{':
                        state->rule = GENERATOR_PARSER_PAIR;
                        return 0;
                default:
                        state->rule = GENERATOR_PARSER_FAIL;
                        return -EINVAL;
                }
        }

        return 0;
}

static int generator_parser_TUPLE(Generator *gen, char c) {
        GeneratorState *next, *state;

        if (c != ')') {
                next = generator_push(gen);
                next->rule = GENERATOR_PARSER_TYPE;
                return generator_parser_TYPE(gen, c);
        }

        state = generator_pop(gen);

        if (state->rule != GENERATOR_PARSER_TUPLE) {
                /* unit type "()" */
                mpz_set_ui(state->seed, _GENERATOR_BASIC_N + 1);
                return generator_fold(gen);
        }

        /* fold decision to end TUPLE */
        mpz_mul_ui(state->seed, state->seed, 2);

        /* fold each decision to continue TUPLE */
        for (next = generator_pop(gen);
             next->rule == GENERATOR_PARSER_TUPLE;
             next = generator_pop(gen)) {
                generator_pi(gen, state->seed, state->seed, next->seed);
                mpz_mul_ui(state->seed, state->seed, 2);
                mpz_add_ui(state->seed, state->seed, 1);
        }

        /* fold decision to use TUPLE */
        mpz_mul_ui(state->seed, state->seed, _GENERATOR_COMPOUND_N);
        mpz_add_ui(state->seed, state->seed, GENERATOR_COMPOUND_r);
        mpz_add_ui(next->seed, state->seed, _GENERATOR_BASIC_N + 2);

        return generator_fold(gen);
}

static int generator_parser_PAIR(Generator *gen, char c) {
        GeneratorState *next, *state = gen->tip;
        unsigned long val;

        val = generator_unmap_basic(c);
        if (val >= _GENERATOR_BASIC_N) {
                state->rule = GENERATOR_PARSER_FAIL;
                return -EINVAL;
        }

        next = generator_pop(gen);
        mpz_set_ui(next->seed, val);
        next = generator_push(gen);
        next->rule = GENERATOR_PARSER_PAIR_CLOSE;
        next = generator_push(gen);
        next->rule = GENERATOR_PARSER_TYPE;
        return 0;
}

static int generator_parser_PAIR_CLOSE(Generator *gen, char c) {
        GeneratorState *next, *state = gen->tip;

        if (c != '}') {
                state->rule = GENERATOR_PARSER_FAIL;
                return -EINVAL;
        }

        next = generator_pop(gen);

        /* fold KEY and VALUE */
        mpz_mul_ui(state->seed, state->seed, _GENERATOR_BASIC_N);
        mpz_add(next->seed, next->seed, state->seed);

        /* fold decision to use PAIR */
        mpz_mul_ui(next->seed, next->seed, _GENERATOR_COMPOUND_N);
        mpz_add_ui(next->seed, next->seed, GENERATOR_COMPOUND_e);
        mpz_add_ui(next->seed, next->seed, _GENERATOR_BASIC_N + 2);

        return generator_fold(gen);
}

/**
 * generator_new() - create generator
 *
 * This function allocates a new generator. Each allocated generator is
 * independent of the other ones, and never touches *any* global state.
 *
 * The generator has an initial seed of 0 and can be used directly to start a
 * new sequence. Usually, you should seed it with the requested value, first,
 * and then start a new sequence via generator_step().
 *
 * The generator uses libgmp (GnuMP) internally. This library is not OOM safe,
 * hence, this implementation will abort the application if malloc() fails.
 *
 * Return: Pointer to generator is returned.
 */
Generator *generator_new(void) {
        Generator *gen;

        gen = calloc(1, sizeof(*gen));
        assert(gen);

        mpz_init(gen->seed);
        mpz_init(gen->root);
        mpz_init(gen->root_squared);
        mpz_init(gen->index);

        return gen;
}

/**
 * generator_free() - destroy generater
 * @gen:        generator to destroy, or NULL
 *
 * This destroys the passed generator and releases all allocated resources. If
 * NULL is passed, this is a no-op.
 *
 * Return: NULL is returned.
 */
Generator *generator_free(Generator *gen) {
        GeneratorState *state;

        if (!gen)
                return NULL;

        while ((state = gen->unused)) {
                gen->unused = state->parent;
                generator_state_free(state);
        }

        while ((state = gen->tip)) {
                gen->tip = state->parent;
                generator_state_free(state);
        }

        mpz_clear(gen->index);
        mpz_clear(gen->root_squared);
        mpz_clear(gen->root);
        mpz_clear(gen->seed);
        free(gen);

        return NULL;
}

/**
 * generator_reset() - reset generator
 * @gen:        generator to reset
 *
 * This resets the current state of the generator @gen. It does *not* reset the
 * seed value!
 *
 * The next call to generator_step() will start a new sequence with the current
 * seed.
 */
void generator_reset(Generator *gen) {
        while (gen->tip)
                generator_pop(gen);
}

/**
 * generator_seed_u32() - seed generator
 * @gen:        generator to seed
 * @seed:       new seed
 *
 * This seeds the generator @gen with @seed. It is similar to
 * generator_seed_str(), but takes binary input, rather than string input. But
 * this limits the possible range to the range of an uint32_t.
 */
void generator_seed_u32(Generator *gen, uint32_t seed) {
        mpz_set_ui(gen->seed, seed);
}

/**
 * generator_seed_str() - seed generator
 * @gen:        generator to seed
 * @str:        string representation of the seed
 * @base:       base of the integer representation
 *
 * This seeds the given generator with the integer given in @str. The integer
 * must be given as ASCII string with a representation of base @base. Arbitrary
 * precision is supported.
 *
 * If there is currently a sequence ongoing, this has *no* effect on it. It
 * will only take effect on the *next* sequence you start.
 *
 * If @str is not formatted as an integer in base @base, this function will use
 * its first byte as binary seed. An error code is still returned!
 *
 * Return: 0 on success, negative error code if @str is wrongly formatted.
 */
int generator_seed_str(Generator *gen, const char *str, int base) {
        int r;

        r = mpz_set_str(gen->seed, str, base);
        if (r < 0) {
                generator_seed_u32(gen, *str);
                return -EINVAL;
        }

        return 0;
}

/**
 * generator_step() - perform single step
 * @gen:        generator to operate on
 *
 * This performs a single generation step on the passed generator. If the
 * generator is unused (no step has been performed, yet, or it was reset), this
 * will start a new sequence based on the current seed. Otherwise, this
 * continues the previous sequence.
 *
 * Each call to this function returns the next element of a valid GVariant
 * type. It is guaranteed to return a valid type. Once done, this returns 0.
 * You need to call generator_reset() if you want to start a new sequence.
 *
 * If a new sequence is started, the current seed value is *copied*. That is,
 * any modification later on has *NO* effect on the current sequence.
 *
 * Return: Next GVariant element of the current sequence, 0 if done.
 */
char generator_step(Generator *gen) {
        GeneratorState *state = gen->tip;

        if (!state) {
                state = generator_push(gen);
                state->rule = GENERATOR_RULE_DONE;
                state = generator_push(gen);
                state->rule = GENERATOR_RULE_TYPE;
                mpz_set(state->seed, gen->seed);
        }

        switch (state->rule) {
        case GENERATOR_RULE_DONE:
                return 0;
        case GENERATOR_RULE_TYPE:
                return generator_rule_TYPE(gen);
        case GENERATOR_RULE_TUPLE:
                return generator_rule_TUPLE(gen);
        case GENERATOR_RULE_TUPLE_CLOSE:
                return generator_rule_TUPLE_CLOSE(gen);
        case GENERATOR_RULE_PAIR:
                return generator_rule_PAIR(gen);
        case GENERATOR_RULE_PAIR_CLOSE:
                return generator_rule_PAIR_CLOSE(gen);
        default:
                assert(0);
                return 0;
        }
}

/**
 * generator_feed() - feed next character into type parser
 * @gen:        generator to operate on
 * @c:          next character to feed
 *
 * This is the inverse of generator_step(). Rather than generating a type from
 * a seed, this parses a type one by one and generates the seed it corresponds
 * to. Simply feed your entire type into a reset generator and use
 * generator_print() afterwards to access the resulting seed.
 *
 * To make sure your type is fully parsed, you can feed your final binary 0
 * into this just fine. If the type is done, this will succeed, otherwise it
 * will return an error.
 *
 * You better not mix generator_feed() and generator_step() without resetting
 * the generator in between. It will work fine, but the resulting data will be
 * garbled.
 *
 * Return: 0 on success, negative error code if the type is invalid.
 */
int generator_feed(Generator *gen, char c) {
        GeneratorState *state = gen->tip;

        if (!state) {
                state = generator_push(gen);
                state->rule = GENERATOR_PARSER_DONE;
                state = generator_push(gen);
                state->rule = GENERATOR_PARSER_TYPE;
        }

        switch (state->rule) {
        case GENERATOR_PARSER_DONE:
                if (!c)
                        return 0;
                state->rule = GENERATOR_PARSER_FAIL;
                /* fallthrough */
        case GENERATOR_PARSER_FAIL:
                return -EINVAL;
        case GENERATOR_PARSER_TYPE:
                return generator_parser_TYPE(gen, c);
        case GENERATOR_PARSER_TUPLE:
                return generator_parser_TUPLE(gen, c);
        case GENERATOR_PARSER_PAIR:
                return generator_parser_PAIR(gen, c);
        case GENERATOR_PARSER_PAIR_CLOSE:
                return generator_parser_PAIR_CLOSE(gen, c);
        default:
                assert(0);
                return 0;
        }
}

/**
 * generator_print() - print final seed
 * @gen:        generator to query
 * @f:          file to print to
 * @base:       base to print number in
 *
 * This call prints the final seed after a full sequence was parsed via
 * generator_feed(). It prints "<invalid>" if the sequence failed or is not
 * finished, yet.
 */
void generator_print(Generator *gen, FILE *f, int base) {
        GeneratorState *state = gen->tip;

        if (state && state->rule == GENERATOR_PARSER_DONE)
                mpz_out_str(f, base, state->seed);
        else
                fprintf(f, "<invalid>");
}
