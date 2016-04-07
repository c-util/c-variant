#pragma once

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

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "org.bus1/c-variant.h"

typedef struct CVariantElement CVariantElement;
typedef struct CVariantLevel CVariantLevel;
typedef struct CVariantSignatureState CVariantSignatureState;
typedef struct CVariantState CVariantState;
typedef struct CVariantType CVariantType;
typedef struct CVariantVarg CVariantVarg;
typedef struct CVariantVargLevel CVariantVargLevel;

/*
 * Macros
 */

#define _likely_(_x) (__builtin_expect(!!(_x), 1))
#define _public_ __attribute__((__visibility__("default")))
#define _unlikely_(_x) (__builtin_expect(!!(_x), 0))

#define ALIGN_TO(_val, _alignment) ((_val + (_alignment) - 1) & ~((_alignment) - 1))
#define ALIGN_PTR_TO(_val, _alignment) ((void *)ALIGN_TO((unsigned long)(_val), (_alignment)))

/*
 * Elements
 */

enum {
        /* invalid */
        C_VARIANT_INVALID               =   0,

        /* basic */
        C_VARIANT_BOOL                  = 'b',
        C_VARIANT_BYTE                  = 'y',
        C_VARIANT_INT16                 = 'n',
        C_VARIANT_UINT16                = 'q',
        C_VARIANT_INT32                 = 'i',
        C_VARIANT_UINT32                = 'u',
        C_VARIANT_INT64                 = 'x',
        C_VARIANT_UINT64                = 't',
        C_VARIANT_HANDLE                = 'h', /* obsolete, use 'u' */
        C_VARIANT_DOUBLE                = 'd',
        C_VARIANT_STRING                = 's',
        C_VARIANT_PATH                  = 'o', /* obsolete, use 's' */
        C_VARIANT_SIGNATURE             = 'g', /* obsolete, use 's' */

        /* containers */
        C_VARIANT_VARIANT               = 'v',
        C_VARIANT_MAYBE                 = 'm',
        C_VARIANT_ARRAY                 = 'a',
        C_VARIANT_TUPLE_OPEN            = '(',
        C_VARIANT_TUPLE_CLOSE           = ')',
        C_VARIANT_PAIR_OPEN             = '{', /* obsolete, use '(' */
        C_VARIANT_PAIR_CLOSE            = '}', /* obsolete, use ')' */

        /* API */
        C_VARIANT_TUPLE                 = 'r', /* unused */
        C_VARIANT_PAIR                  = 'e', /* unused */
        C_VARIANT_BASIC                 = '?', /* unused */
        C_VARIANT_ANY                   = '*', /* unused */

        /* helpers */
        C_VARIANT_MAX                   = 255,
        C_VARIANT_N                     = 256,
};

struct CVariantElement {
        uint8_t alignment : 2;  /* alignment (in power of 2; only fixed size) */

        uint8_t valid : 1;      /* table-entry is valid */
        uint8_t real : 1;       /* allowed in type strings */
        uint8_t basic : 1;      /* basic element */
        uint8_t fixed : 1;      /* fixed-size element */

        uint8_t unused : 2;
};

/*
 * Types
 */

struct CVariantType {
        size_t alignment;       /* alignment of this type, (in power of 2) */
        size_t size;            /* size in bytes, if fixed-size, or 0 */
        size_t bound_size;      /* size of bound child, if fixed-size, or 0 */
        size_t n_levels;        /* maximum nesting level */
        size_t n_type;          /* length of type string */
        const char *type;       /* type string */
};

/*
 * Signatures
 */

enum {
        C_VARIANT_SIGNATURE_STATE_BOUND,        /* bound container */
        C_VARIANT_SIGNATURE_STATE_TUPLE,        /* tuple */
        C_VARIANT_SIGNATURE_STATE_PAIR_0,       /* first entry of pair */
        C_VARIANT_SIGNATURE_STATE_PAIR_1,       /* second entry of pair */
        _C_VARIANT_SIGNATURE_STATE_N,
};

struct CVariantSignatureState {
        uint8_t state : 2;
        uint8_t alignment : 2;
        uint8_t aligned : 3;
        uint8_t unused : 1;
};

int c_variant_signature_next(const char *signature, size_t n_signature, CVariantType *infop);
int c_variant_signature_one(const char *signature, size_t n_signature, CVariantType *infop);

/*
 * State Levels
 */

struct CVariantLevel {
        /* mostly static */
        size_t size;            /* overall size of container */
        size_t i_tail;          /* end of container (index) */
        uint16_t v_tail;        /* end of container (vector) */
        uint8_t wordsize;       /* cached wordsize (in power of 2) */
        char enclosing;         /* enclosing container type */

        /* mostly dynamic */
        uint16_t n_type;        /* remaining length of type */
        uint16_t v_front;       /* current position (vector) */
        size_t i_front;         /* current position (index) */
        size_t index;           /* container specific index */
        size_t offset;          /* current position (offset) */
        const char *type;       /* current index into type */
};

struct CVariantState {
        CVariantState *link;            /* parent/child state */
        uint8_t i_levels;               /* current iterator level */
        uint8_t n_levels;               /* number of allocated levels */
        CVariantLevel levels[0];        /* level array */
};

void c_variant_level_root(CVariantLevel *level, size_t size, const char *type, size_t n_type);
bool c_variant_on_root_level(CVariant *cv);
int c_variant_ensure_level(CVariant *cv);
void c_variant_push_level(CVariant *cv);
void c_variant_pop_level(CVariant *cv);

/*
 * Variants
 */

#define C_VARIANT_MAX_INLINE_LEVELS (UINT8_MAX)
#define C_VARIANT_MAX_VECS (UINT16_MAX)
#define C_VARIANT_FRONT_SHARE (80)

struct CVariant {
        CVariantState *state;           /* current state */
        CVariantState *unused;          /* unused state objects */
        struct iovec *vecs;             /* iovecs backing the variant */

        uint16_t n_type;                /* initial type length */
        uint16_t n_vecs;                /* number of iovecs in @vecs */
        uint8_t poison : 8;             /* 'errno' code that poisoned it */
        uint8_t a_vecs : 6;             /* number of allocated vectors */
        bool sealed : 1;                /* is it sealed? */
        bool allocated_vecs : 1;        /* are vectors allocated? */
};

int c_variant_alloc(CVariant **cvp,
                    char **typep,
                    void **extrap,
                    size_t n_type,
                    size_t n_hint_levels,
                    size_t n_vecs,
                    size_t n_extra);
void c_variant_dealloc(CVariant *cv);
int c_variant_poison_internal(CVariant *cv, int poison);

#define c_variant_poison(_cv, _poison)                          \
        ({                                                      \
                static_assert((_poison) < 0,                    \
                              "Invalid poison code");           \
                static_assert(-(_poison) < (1LL << 8),          \
                              "Invalid poison code");           \
                c_variant_poison_internal((_cv), (_poison));    \
        })

/*
 * Vararg Iterator
 */

struct CVariantVargLevel {
        const char *type;
        size_t n_type;
        size_t n_array;
};

struct CVariantVarg {
        CVariantVargLevel levels[C_VARIANT_MAX_VARG];
        size_t i_levels;
};

int c_variant_varg_init(CVariantVarg *varg, const char *type, size_t n_type);
int c_variant_varg_next(CVariantVarg *varg);
const char *c_variant_varg_type(CVariantVarg *varg, size_t *n_typep);
void c_variant_varg_push(CVariantVarg *varg, const char *type, size_t n_type, size_t n_array);
void c_variant_varg_enter_bound(CVariantVarg *varg, CVariant *cv, size_t n_array);
void c_variant_varg_enter_unbound(CVariantVarg *varg, CVariant *cv, char closing);
void c_variant_varg_enter_default(CVariantVarg *varg, bool is_bound, size_t n_array);

/*
 * Word Handling
 */

static inline uint8_t c_variant_word_size(size_t base, size_t extra) {
        /* We never return *real* wordsize 0, caller must be aware of that. */
        if (base + extra <= UINT8_MAX)
                return 0;
        else if (base + extra * 2 <= UINT16_MAX)
                return 1;
        else if (base + extra * 4 <= UINT32_MAX)
                return 2;
        else
                return 3; /* implies sizeof(size_t) >= 8 */
}
