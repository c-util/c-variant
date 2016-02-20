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
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "c-variant.h"

typedef struct CVariantElement CVariantElement;
typedef struct CVariantLevel CVariantLevel;
typedef struct CVariantSignatureState CVariantSignatureState;
typedef struct CVariantType CVariantType;
typedef struct CVariantVarg CVariantVarg;
typedef struct CVariantVargLevel CVariantVargLevel;

/*
 * Macros
 */

#define _align_(_x) __attribute__((__aligned__(_x)))
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
        C_VARIANT_TUPLE                 = 'r',
        C_VARIANT_PAIR                  = 'e',
        C_VARIANT_BASIC                 = '?',
        C_VARIANT_ANY                   = '*',

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

int c_variant_signature_next(const char *signature, size_t n_signature, CVariantType *out);

/*
 * Levels
 */

struct CVariantLevel {
        /* static */
        size_t size;            /* overall size of container */
        size_t i_end;           /* end of container (index) */
        uint16_t v_end;         /* end of container (vector) */
        uint8_t wordsize;       /* cached wordsize (in power of 2) */
        char enclosing;         /* enclosing container type */

        /* dynamic */
        uint16_t i_type;        /* index into type */
        uint16_t n_type;        /* remaining length of type */
        size_t index;           /* container specific index */
        size_t offset;          /* current position (offset) */
        size_t i_front;         /* current position (index) */
        uint16_t v_front;       /* current position (vector) */
};

/*
 * Variants
 */

#define C_VARIANT_MAX_INLINE_LEVELS (16)

struct CVariant {
        const char *type;               /* type of the variant */
        struct iovec *vecs;             /* iovecs backing the variant */
        CVariantLevel *levels;          /* parser levels of this variant */
        CVariant *parent;               /* parent variant */
        CVariant *unused;               /* unused child */

        uint16_t n_vecs;                /* number of iovecs in @vecs */
        uint16_t n_type;                /* length of @type */
        uint8_t i_levels;               /* current parser level */
        uint8_t n_levels;               /* allocated levels */
        uint8_t poison : 7;             /* 'errno' code that poisoned it */
        bool sealed : 1;                /* sealed? */

        uint8_t appendix[0];            /* trailing backing memory */
};

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
