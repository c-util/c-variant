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
 * Extensions to the GVariant Specification:
 *  - If the last framing offset of an array is not fully covered by a single
 *    iovec, the array is treated empty (default value).
 *  - If a single framing offset is not fully covered by a single iovec, its
 *    corresponding child frame is treated empty.
 *  - If a single basic type is not fully covered by a single iovec, it yields
 *    the default value.
 *
 * Errata:
 *  - If a dynamic-sized structure has a fixed-size type as last element, but
 *    the available size to that element is bigger than required, then those
 *    additional padding bytes are ignored.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include "org.bus1/c-variant.h"
#include "c-variant-private.h"

/*
 * Elements
 * ========
 *
 * GVariant types are built out of basic elements. See the specification for
 * details. We use 'element' to refer to a single character in a GVariant type
 * string.
 */

static_assert(C_VARIANT_MAX + 1 == 1 << (sizeof(char) * 8),
              "Invalid maximum element");
static_assert(sizeof(CVariantElement) == 1,
              "Invalid bitfield grouping");

static const CVariantElement c_variant_elements[C_VARIANT_N] = {
        /* invalid */
        [C_VARIANT_INVALID]     = { },

        /* basic */
        [C_VARIANT_BOOL]        = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_BYTE]        = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_INT16]       = { .alignment = 1,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_UINT16]      = { .alignment = 1,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_INT32]       = { .alignment = 2,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_UINT32]      = { .alignment = 2,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1},
        [C_VARIANT_INT64]       = { .alignment = 3,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_UINT64]      = { .alignment = 3,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_HANDLE]      = { .alignment = 2,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_DOUBLE]      = { .alignment = 3,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 1, },
        [C_VARIANT_STRING]      = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 0, },
        [C_VARIANT_PATH]        = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 0, },
        [C_VARIANT_SIGNATURE]   = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 1,
                                    .fixed = 0, },

        /* containers */
        [C_VARIANT_VARIANT]     = { .alignment = 3,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_MAYBE]       = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_ARRAY]       = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_TUPLE_OPEN]  = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_TUPLE_CLOSE] = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_PAIR_OPEN]   = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_PAIR_CLOSE]  = { .alignment = 0,
                                    .valid = 1,
                                    .real = 1,
                                    .basic = 0,
                                    .fixed = 0, },

        /* API */
        [C_VARIANT_TUPLE]       = { .alignment = 0,
                                    .valid = 1,
                                    .real = 0,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_PAIR]        = { .alignment = 0,
                                    .valid = 1,
                                    .real = 0,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_BASIC]       = { .alignment = 0,
                                    .valid = 1,
                                    .real = 0,
                                    .basic = 0,
                                    .fixed = 0, },
        [C_VARIANT_ANY]         = { .alignment = 0,
                                    .valid = 1,
                                    .real = 0,
                                    .basic = 0,
                                    .fixed = 0, },
};

static const CVariantElement *c_variant_element(char element) {
        /* always a valid offset due to "char" */
        return &c_variant_elements[(uint8_t)element];
}

/*
 * Signatures
 * ==========
 *
 * A signature is what we call a stream of GVariant types. That is, strip the
 * opening and closing brackets from a tuple and you got a signature.
 * We implement a non-recursive signature parser here, which parses one
 * GVariant type from a signature at a time. It returns as much
 * type-information about each type as possible. However, we do not build
 * recursive type information. That is, if you parse a single, deeply nested
 * type, you only get information about the top-level. You need to inspect each
 * child-type, if you need specific information on them. This is based on the
 * assumption that building a dynamic tree is much slower, than parsing types
 * multiple times. Unless your type is deeply nested, this assumption holds
 * true in our performance tests.
 */

static_assert(sizeof(CVariantSignatureState) == 1,
              "Invalid bitmap optimization");
static_assert(_C_VARIANT_SIGNATURE_STATE_N <= (1 << 2),
             "State bitfield of signature state is too small");

/**
 * c_variant_signature_next() - parse next type in a signature
 * @signature:          signature to parse
 * @n_signature:        length of @signature
 * @infop:              output for type information
 *
 * This parses the leading GVariant type in @signature, and returns the type
 * information in @infop. The caller can use that information to skip over the
 * type in the signature and parse the next one.
 *
 * Once the end of a signature is reached, 0 is returned. If a type was
 * successfully parsed, its type-information is returned in @infop and 1 is
 * returned. If the first type in the signature is malformed, an error code is
 * returned.
 *
 * The information returned in @outp contains:
 *  - alignment of the type
 *  - size of the type, or 0 if non-fixed
 *  - in case of a bound container type, the size of the bound child, or 0 if
 *    non-fixed
 *  - maximum nesting level
 *  - length of the type-string
 *  - pointer to the start of the type (usually equals @signature)
 *
 * Return: 1 if a type was parsed successfully, 0 if the signature was empty,
 *         negative error code on failure.
 */
int c_variant_signature_next(const char *signature, size_t n_signature, CVariantType *infop) {
        size_t i, t, max_level, size, level, known_level;
        CVariantSignatureState state, saved, *stack = NULL;
        bool fixed_size, end_of_pair;

        /*
         * We support arbitrary type-strings, but for performance reasons we
         * limit the length to some insanely large value. This allows us to
         * assume that none of our computations suffers from integer overflows.
         *
         * The static-assert checks that the largest size we compute cannot
         * overflow.
         */

        static_assert(C_VARIANT_MAX_SIGNATURE * 8 / 8 == C_VARIANT_MAX_SIGNATURE,
                      "Invalid maximum signature length");

        if (_unlikely_(n_signature > C_VARIANT_MAX_SIGNATURE))
                return -EMSGSIZE;

        /*
         * Parsing a signature requires recursing into each nesting level. As
         * we want to avoid true recursion (no tail recursion is possible), we
         * instead keep backtracking information for each level we recursed
         * into. 8-bit are required per level, so it is perfectly fine to place
         * this on the stack. But make sure we do not exceed 4k. In case the
         * max depth is increased, we should probably fall back to malloc() if
         * a high depth is used.
         *
         * Note that the maximum *valid* depth cannot be higher than
         * [n_signature - 1]. For performance reasons, we always use this as
         * upper bound.
         */

        static_assert(sizeof(CVariantSignatureState) * C_VARIANT_MAX_LEVEL <= 4096,
                      "Invalid maximum depth level");

        max_level = C_VARIANT_MAX_LEVEL;
        if (_likely_(max_level > n_signature))
                max_level = n_signature;

        if (max_level > 0)
                stack = alloca(sizeof(*stack) * max_level);

        /*
         * Parse the type signature one element at a time, and keep track of
         * the nesting levels in @stack. @state is the state of the current
         * level.
         * GVariant supports 2 different kinds of containers. One is the
         * unbound type, which has explicit OPEN and CLOSE markers. Those are
         * easy to parse. The other kind is the bound one. Those are implicitly
         * closed by the next following leaf type.
         *
         * Once we parsed a full type from the signature, we return the
         * information to the caller. This way, the caller can parse the
         * signature one by one.
         *
         * Once the type is parsed, we fill that information into @infop and
         * return 1. If we're at the end of the signature, and no type was
         * parsed, we return 0.
         */

        state.state = C_VARIANT_SIGNATURE_STATE_TUPLE;
        state.alignment = 0;
        state.aligned = 0;

        size = 0;
        level = 0;
        known_level = 0;
        fixed_size = true;
        end_of_pair = false;

        for (i = 0; i < n_signature; ++i) {
                const CVariantElement *element;
                char element_id;
                bool is_leaf;

                element_id = signature[i];
                element = c_variant_element(element_id);

                /* fail on invalid type specifiers */
                if (_unlikely_(!element->real))
                        return -EMEDIUMTYPE;

                switch (element_id) {
                case C_VARIANT_MAYBE:
                case C_VARIANT_ARRAY:
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        /* limit maximum depth */
                        if (_unlikely_(level >= max_level))
                                return -ELOOP;

                        /* pair is limited to 2 types and first must be basic */
                        if (_unlikely_(end_of_pair || state.state == C_VARIANT_SIGNATURE_STATE_PAIR_0))
                                return -EMEDIUMTYPE;

                        /* backtracking */
                        stack[level++] = state;
                        if (level > known_level)
                                known_level = level;

                        /* enter container */
                        if (element_id == C_VARIANT_TUPLE_OPEN)
                                state.state = C_VARIANT_SIGNATURE_STATE_TUPLE;
                        else if (element_id == C_VARIANT_PAIR_OPEN)
                                state.state = C_VARIANT_SIGNATURE_STATE_PAIR_0;
                        else
                                state.state = C_VARIANT_SIGNATURE_STATE_BOUND;

                        /*
                         * We cannot know the alignment of the container in
                         * advance, so we always use the maximum alignment and
                         * shift it afterwards, if possible.
                         */
                        t = ALIGN_TO(size, 8);
                        state.alignment = 0;
                        state.aligned = t - size;
                        size = t;

                        is_leaf = false;
                        break;

                case C_VARIANT_TUPLE_CLOSE:
                        /* level 0 is an implicit tuple, it cannot be closed */
                        if (_unlikely_(level == 0))
                                return -EMEDIUMTYPE;

                        /* brackets must match */
                        if (_unlikely_(state.state != C_VARIANT_SIGNATURE_STATE_TUPLE))
                                return -EMEDIUMTYPE;

                        /* special case: unit type has fixed length of 1 */
                        if (signature[i - 1] == C_VARIANT_TUPLE_OPEN)
                                size += 1;

                        /* fallthrough */
                case C_VARIANT_PAIR_CLOSE:
                        /* brackets must match */
                        if (_unlikely_(element_id == C_VARIANT_PAIR_CLOSE && !end_of_pair))
                                return -EMEDIUMTYPE;

                        /*
                         * The container was max-aligned when opened. If the
                         * alignment now turns out to be smaller, we try
                         * shifting it to the smallest valid alignment.
                         *
                         * Afterwards, make sure to pad the container to a
                         * multiple of its alignment.
                         */
                        if (fixed_size) {
                                size -= state.aligned & ~((1 << state.alignment) - 1);
                                size = ALIGN_TO(size, 1 << state.alignment);
                        }

                        /* get backtracking information */
                        saved = stack[--level];

                        /* inherit alignment of nested container */
                        if (state.alignment > saved.alignment)
                                saved.alignment = state.alignment;

                        state = saved;

                        end_of_pair = false;
                        is_leaf = true;
                        break;

                case C_VARIANT_BOOL:
                case C_VARIANT_BYTE:
                case C_VARIANT_INT16:
                case C_VARIANT_UINT16:
                case C_VARIANT_INT32:
                case C_VARIANT_UINT32:
                case C_VARIANT_INT64:
                case C_VARIANT_UINT64:
                case C_VARIANT_HANDLE:
                case C_VARIANT_DOUBLE:
                case C_VARIANT_STRING:
                case C_VARIANT_PATH:
                case C_VARIANT_SIGNATURE:
                case C_VARIANT_VARIANT:
                        /* verify pairs are filled correctly */
                        if (_unlikely_(end_of_pair))
                                return -EMEDIUMTYPE;
                        if (_unlikely_(state.state == C_VARIANT_SIGNATURE_STATE_PAIR_0 &&
                                       !element->basic))
                                return -EMEDIUMTYPE;

                        /* remember if dynamically sized */
                        if (!element->fixed)
                                fixed_size = false;

                        /* remember alignment */
                        if (element->alignment > state.alignment)
                                state.alignment = element->alignment;

                        /* if fixed size, align and add size */
                        if (fixed_size) {
                                size = ALIGN_TO(size, 1 << element->alignment);
                                /* fixed size of elements equals their alignment */
                                size += 1 << element->alignment;
                        }

                        is_leaf = true;
                        break;

                default:
                        /* should already be caught by 'element->real' test */
                        return -EMEDIUMTYPE;
                }

                /*
                 * If the parsed element is a leaf, it implicitly closes all
                 * open, bound containers. Furthermore, if we're back at level
                 * 0, we are done with one type and must return to the caller.
                 */
                if (is_leaf) {
                        size_t bound_size = 0;

                        while (state.state == C_VARIANT_SIGNATURE_STATE_BOUND) {
                                /*
                                 * Bound containers are never fixed size.
                                 * However, if the direct child is, we remember
                                 * its size, so we can tell the caller about
                                 * it.
                                 */
                                bound_size = fixed_size ? size : 0;
                                fixed_size = false;

                                saved = stack[--level];

                                /* inherit alignment of nested container */
                                if (state.alignment > saved.alignment)
                                        saved.alignment = state.alignment;

                                state = saved;
                        }

                        /* advance possible pair */
                        if (state.state == C_VARIANT_SIGNATURE_STATE_PAIR_0) {
                                state.state = C_VARIANT_SIGNATURE_STATE_PAIR_1;
                        } else if (state.state == C_VARIANT_SIGNATURE_STATE_PAIR_1) {
                                end_of_pair = true;
                        }

                        /* leaf parsed: if back at depth 0, we're done */
                        if (level == 0) {
                                infop->alignment = state.alignment;
                                infop->size = fixed_size ? size : 0;
                                infop->bound_size = bound_size;
                                infop->n_levels = known_level;
                                infop->n_type = ++i;
                                infop->type = signature;
                                return 1;
                        }
                }
        }

        /*
         * If the signature was empty, we tell the caller about it by returning
         * 0. If it was non-empty, but we still reached the end of the
         * signature without fully parsing a single type, then the signature is
         * invalid and we need to tell the caller.
         */
        if (_unlikely_(i))
                return -EMEDIUMTYPE;

        infop->alignment = 0;
        infop->size = 0;
        infop->bound_size = 0;
        infop->n_levels = 0;
        infop->n_type = 0;
        infop->type = "";
        return 0;
}

int c_variant_signature_one(const char *signature, size_t n_signature, CVariantType *infop) {
        int r;

        r = c_variant_signature_next(signature, n_signature, infop);
        if (_unlikely_(r < 0))
                return r;
        if (_unlikely_(r == 0 || infop->n_type != n_signature))
                return -EMEDIUMTYPE;
        return 0;
}

/*
 * State
 * =====
 *
 * XXX
 */

static int c_variant_state_new(CVariantState **statep,
                               void **extrap,
                               size_t n_extra,
                               size_t n_hint_levels) {
        size_t size, off_extra;
        CVariantState *state;

        static_assert(__alignof(CVariantState) <= 8, "Invalid CVariantState alignment");

        /* state->levels[state->i_levels] better always works! */
        n_hint_levels = (n_hint_levels < 1) ? 1 :
                        (n_hint_levels > C_VARIANT_MAX_INLINE_LEVELS) ?
                                        C_VARIANT_MAX_INLINE_LEVELS :
                                        n_hint_levels;

        /* space for: state-object and appended level-array (auto-align) */
        size = offsetof(CVariantState, levels);
        size += sizeof(*state->levels) * n_hint_levels;

        /* space for: extra buffer, if any (8-byte aligned) */
        off_extra = ALIGN_TO(size, 8);
        size = off_extra + n_extra;

        state = malloc(size);
        if (!state)
                return -ENOMEM;

        /* malloc() better is 8-byte aligned, always! */
        assert(state == ALIGN_PTR_TO(state, 8));

        state->link = NULL;
        state->i_levels = 0;
        state->n_levels = n_hint_levels;

        *statep = state;
        if (extrap)
                *extrap = (char *)state + off_extra;
        return 0;
}

/*
 * Variants
 * ========
 *
 * XXX
 */

int c_variant_alloc(CVariant **cvp,
                    char **typep,
                    void **extrap,
                    size_t n_type,
                    size_t n_hint_levels,
                    size_t n_vecs,
                    size_t n_extra) {
        CVariantState *state;
        CVariant *cv;
        size_t size;
        int r;

        /*
         * Allocate a new CVariant, including:
         *  - uninitialized buffer for type, returned in @typep
         *  - state object with levels pre-allocated
         *  - set of _uninitialized_ iovecs (@n_vecs)
         *  - trailing, 8-byte aligned, extra data, returned in @extrap
         *
         * The caller is responsible to initialize the initial level, all
         * iovecs, the type content, and the extra data, if any.
         */

        static_assert(__alignof(struct iovec) <= 8, "Invalid iovec alignment");
        static_assert(__alignof(CVariant) <= 8, "Invalid CVariant alignment");
        static_assert((1ULL << (8 * sizeof(((CVariant *)0)->n_type))) > C_VARIANT_MAX_SIGNATURE,
                      "Invalid maximum signature length");
        static_assert((1ULL << (8 * sizeof(((CVariantState *)0)->i_levels))) > C_VARIANT_MAX_LEVEL,
                      "Invalid maximum depth level");
        static_assert((1ULL << (8 * sizeof(((CVariant *)0)->n_vecs))) > C_VARIANT_MAX_VECS,
                      "Invalid maximum number of iovecs");

        if (n_vecs > C_VARIANT_MAX_VECS)
                return -ENOBUFS;

        size = ALIGN_TO(sizeof(CVariant), __alignof(struct iovec));
        size += sizeof(struct iovec) * n_vecs + n_vecs;
        size += n_type;
        size = ALIGN_TO(size, 8);
        r = c_variant_state_new(&state,
                                (void **)&cv,
                                size + n_extra,
                                n_hint_levels);
        if (r < 0)
                return r;

        cv->state = state;
        cv->unused = NULL;
        cv->vecs = ALIGN_PTR_TO(cv + 1, __alignof(struct iovec));
        cv->n_type = n_type;
        cv->n_vecs = n_vecs;
        cv->poison = 0;
        cv->a_vecs = 0;
        cv->sealed = false;
        cv->allocated_vecs = false;

        /* mark all vecs as non-allocated */
        memset(cv->vecs + cv->n_vecs, 0, cv->n_vecs);

        *cvp = cv;
        *typep = (char *)(cv->vecs + cv->n_vecs) + cv->n_vecs;
        if (extrap)
                *extrap = (char *)cv + size;
        return 0;
}

void c_variant_dealloc(CVariant *cv) {
        CVariantState *state;
        size_t i;

        /* pop all cached, unused states (they're never static) */
        while ((state = cv->unused)) {
                cv->unused = state->link;
                free(state);
        }

        /* pop anything but the initial state (which is static) */
        while ((state = cv->state->link)) {
                free(cv->state);
                cv->state = state;
        }

        /*
         * Free data, but align first as we might have screwed with the base
         * pointers during allocation to fulfill alignment needs.
         */
        for (i = 0; i < cv->n_vecs; ++i)
                if (((char *)(cv->vecs + cv->n_vecs))[i])
                        free((void *)((unsigned long)cv->vecs[i].iov_base & ~7));

        /* free vector-array, if it was reallocated */
        if (cv->allocated_vecs)
                free(cv->vecs);

        /* @cv is embedded in the root-state @cv->state */
        free(cv->state);
}

int c_variant_poison_internal(CVariant *cv, int poison) {
        /*
         * Poison @cv with negative error-code @poison. If @cv was already
         * poisoned, the previous poison is preserved and @poison is discarded.
         */

        if (cv->poison)
                return -cv->poison;

        cv->poison = -poison;
        return -cv->poison;
}

bool c_variant_on_root_level(CVariant *cv) {
        /* 'true' if there is NO parent level */
        return cv->state->i_levels == 0 && !cv->state->link;
}

int c_variant_ensure_level(CVariant *cv) {
        int r;

        if (_likely_(cv->state->i_levels + 1 < cv->state->n_levels || cv->unused))
                return 0;

        /* allocate new state with fixed 16 levels */
        r = c_variant_state_new(&cv->unused, NULL, 0, 16);
        if (r < 0)
                return c_variant_poison(cv, -ENOMEM);

        return 0;
}

void c_variant_push_level(CVariant *cv) {
        CVariantState *state;

        /*
         * Enter a new level on top of the current one. The state of the new
         * level is undefined and needs to be initialized by the caller. The
         * caller must guarantee that there is at least one more level
         * allocated. Use c_variant_ensure_level() to pre-allocate levels.
         */

        if (_likely_(cv->state->i_levels + 1 < cv->state->n_levels)) {
                ++cv->state->i_levels;
        } else {
                assert(cv->unused);

                state = cv->unused;
                cv->unused = state->link;
                state->link = cv->state;
                cv->state = state;

                /* reset state */
                state->i_levels = 0;
        }
}

void c_variant_pop_level(CVariant *cv) {
        CVariantState *state;

        /*
         * Exit the current level by discarding all its cached state and
         * returning to the parent level.
         */

        if (_likely_(cv->state->i_levels > 0)) {
                --cv->state->i_levels;
        } else {
                assert(cv->state->link);

                state = cv->state->link;
                cv->state->link = cv->unused;
                cv->unused = cv->state;
                cv->state = state;
        }
}

/*
 * Vararg
 * ======
 *
 * The public API supports compound accessors, that can read a dynamic set of
 * types from a variant stream. Those accessors work similar to scanf(3p), but
 * rather than using a format-string, they provide a valid signature. Based on
 * this signature, we parse the variant stream at the current iterator level.
 *
 * The signature-string can be nested itself. Therefore, we need state tracking
 * to remember at which position, and on which level we are. The CVariantVarg
 * type implements this state tracking on the stack.
 *
 * NOTE: We could ditch all that and just call the accessor functions
 *       recursively. However, va_list becomes undefined whenever you return
 *       from a function that called va_arg(3p). Hence, this is not an option.
 */

int c_variant_varg_init(CVariantVarg *varg, const char *type, size_t n_type) {
        varg->i_levels = 0;
        varg->levels[0].type = type;
        varg->levels[0].n_type = n_type;
        varg->levels[0].n_array = -1;
        return c_variant_varg_next(varg);
}

int c_variant_varg_next(CVariantVarg *varg) {
        CVariantVargLevel *vlevel = &varg->levels[varg->i_levels];
        int c;

        if (vlevel->n_array == (size_t)-1) {
                if (!vlevel->n_type) {
                        c = 0;
                } else {
                        c = *vlevel->type++;
                        --vlevel->n_type;
                }
        } else {
                if (vlevel->n_array == 0) {
                        c = 0;
                } else {
                        c = *(vlevel->type - 1);
                        --vlevel->n_array;
                }
        }

        if (!c) {
                if (varg->i_levels == 0)
                        return 0; /* end of stream */

                --varg->i_levels;
                return -1; /* level done */
        }

        return c; /* next type */
}

const char *c_variant_varg_type(CVariantVarg *varg, size_t *n_typep) {
        CVariantVargLevel *vlevel = &varg->levels[varg->i_levels];

        if (vlevel->n_array == (size_t)-1) {
                *n_typep = vlevel->n_type;
                return vlevel->type;
        } else {
                *n_typep = vlevel->n_type + 1;
                return vlevel->type - 1;
        }
}

void c_variant_varg_push(CVariantVarg *varg, const char *type, size_t n_type, size_t n_array) {
        CVariantVargLevel *vlevel;

        assert(varg->i_levels + 1 < C_VARIANT_MAX_VARG);

        vlevel = &varg->levels[++varg->i_levels];
        vlevel->type = type;
        vlevel->n_type = n_type;
        vlevel->n_array = n_array;

        /* arrays point past their first member */
        if (vlevel->n_array != (size_t)-1) {
                ++vlevel->type;
                --vlevel->n_type;
        }
}

void c_variant_varg_enter_bound(CVariantVarg *varg, CVariant *cv, size_t n_array) {
        CVariantVargLevel *vlevel = &varg->levels[varg->i_levels];
        CVariantLevel *level = cv->state->levels + cv->state->i_levels;

        /* callers better know the type before accessing it */
        assert(vlevel->n_type >= level->n_type);
        assert(!strncmp(vlevel->type, level->type, level->n_type));

        c_variant_varg_push(varg, vlevel->type, level->n_type, n_array);
        if (vlevel->n_array == (size_t)-1) {
                vlevel->type += level->n_type;
                vlevel->n_type -= level->n_type;
        }
}

void c_variant_varg_enter_unbound(CVariantVarg *varg, CVariant *cv, char closing) {
        CVariantVargLevel *vlevel = &varg->levels[varg->i_levels];
        CVariantLevel *level = cv->state->levels + cv->state->i_levels;

        /* callers better know the type before accessing it */
        assert(vlevel->n_type >= level->n_type + 1U);
        assert(!strncmp(vlevel->type, level->type, level->n_type));
        assert(vlevel->type[level->n_type] == closing);

        c_variant_varg_push(varg, vlevel->type, level->n_type, -1);
        if (vlevel->n_array == (size_t)-1) {
                vlevel->type += level->n_type + 1U;
                vlevel->n_type -= level->n_type + 1U;
        }
}

void c_variant_varg_enter_default(CVariantVarg *varg, bool is_bound, size_t n_array) {
        CVariantVargLevel *vlevel = varg->levels + varg->i_levels;
        CVariantType info;
        int r;

        /*
         * This is a generic variant of c_variant_varg_enter_bound() and
         * c_variant_varg_enter_unbound(). Rather than relying on an existing
         * variant, this parses the provided type. This is much slower, hence,
         * it is only used in error paths were the variant cannot be accessed
         * (like OOM, etc.).
         */

        r = c_variant_signature_next(vlevel->type - 1, vlevel->n_type + 1, &info);
        assert(r == 1);

        c_variant_varg_push(varg, info.type + 1, info.n_type - 1 - !is_bound, n_array);
        vlevel->type += info.n_type - 1;
        vlevel->n_type -= info.n_type - 1;
}

/**
 * c_variant_free() - destroy a variant
 * @cv:         variant to operate on, or NULL
 *
 * This destroys the passed variant and frees all allocated resources. Any
 * pointer returned by accessor functions might become invalid.
 *
 * If @cv is NULL, this operation is a no-op.
 *
 * Return: NULL is returned.
 */
_public_ CVariant *c_variant_free(CVariant *cv) {
        if (cv)
                c_variant_dealloc(cv);
        return NULL;
}

/**
 * c_variant_is_sealed() - check whether variant is sealed
 * @cv:         variant to operate on, or NULL
 *
 * This checks whether the variant @cv is sealed. Unsealed variants can be
 * written to, but not read from, and vice versa.
 *
 * Return: True if @cv is sealed, false if not.
 */
_public_ bool c_variant_is_sealed(CVariant *cv) {
        return !cv || cv->sealed;
}

/**
 * c_variant_return_poison() - return poison
 * @cv:         variant to operate on, or NULL
 *
 * Many operations on variants may fail for a large set of reasons. Whenever
 * that happens, an error code is returned. Those errors only ever affect the
 * operation that returned them. You are perfectly free to continue working
 * with the variant.
 *
 * However, usually any error that can happen on a variant is fatal for the
 * overall operation you are trying to perform (e.g., assembling a compound
 * variant). Often there is little reason to continue. Hence, to simplify the
 * API, each variant always remembers the first error code that was caused by
 * any operation on it. This we call 'poison'. This function returns this
 * poison code. API users are free to ignore any error codes of other function
 * calls, and rather check the poison before passing the variant on. In cases
 * where input is validated and errors are not expected, the poison-feature
 * allows to reduce the amount of code required to work with variants a lot, at
 * the cost of some useless operations in the case of errors.
 *
 * Use of the poison-feature is fully optional!
 *
 * Return: 0 if @cv is not poisoned, negative error code if it is.
 */
_public_ int c_variant_return_poison(CVariant *cv) {
        return cv ? -cv->poison : 0;
}

/**
 * c_variant_get_vecs() - retrieve backing iovec array
 * @cv:                 variant to operate on, or NULL
 * @n_vecsp:            output storage for iovec array size
 *
 * This returns a pointer to the backing iovec array of the variant, and its
 * size via @n_vecsp.
 *
 * It is an programming error to call this on an unsealed variant.
 *
 * Return: Pointer to iovec array.
 */
_public_ const struct iovec *c_variant_get_vecs(CVariant *cv, size_t *n_vecsp) {
        assert(n_vecsp);

        if (_unlikely_(!cv)) {
                *n_vecsp = 0;
                return NULL;
        }

        assert(cv->sealed);

        *n_vecsp = cv->n_vecs;
        return cv->vecs;
}
