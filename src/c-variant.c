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
#include "c-variant.h"
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

static int c_variant_signature_one(const char *signature, size_t n_signature, CVariantType *infop) {
        int r;

        r = c_variant_signature_next(signature, n_signature, infop);
        if (_unlikely_(r < 0))
                return r;
        if (_unlikely_(r == 0 || infop->n_type != n_signature))
                return -EMEDIUMTYPE;
        return 0;
}

/*
 * Word Handling
 * =============
 *
 * The serialized format of GVariants defines a 'word-size' that is used to
 * store framing offsets. This word-size depends on the size of the parent
 * container. Hence, it has to be calculated for each container you access.
 *
 * Based on the word-size, accessor functions are provided, which read/write
 * unaligned words.
 *
 * While GVariant allows types of unlimited size, we require all data to be at
 * least mapped into the address-space. That is, the overall size of the root
 * container must be representable in a size_t.
 */

static uint8_t c_variant_word_size(size_t base, size_t extra) {
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

static size_t c_variant_word_fetch(void *addr, size_t wordsize) {
        union {
                uint8_t u8;
                uint16_t u16;
                uint32_t u32;
                uint64_t u64;
        } v;

        memcpy(&v, addr, 1 << wordsize);

        /* read one word of wordsize '1 << wordsize' from @addr */

        switch (wordsize) {
        default:
                assert(0);
                /* fallthrough */
        case 3:
                return le64toh(v.u64);
        case 2:
                return le32toh(v.u32);
        case 1:
                return le16toh(v.u16);
        case 0:
                return v.u8;
        }
}

#if 0
static void c_variant_word_store(void *addr, size_t wordsize, size_t value) {
        union {
                uint8_t u8;
                uint16_t u16;
                uint32_t u32;
                uint64_t u64;
        } v;

        /* write one word of wordsize '1 << wordsize' to @addr */

        switch (wordsize) {
        default:
                assert(0);
                /* fallthrough */
        case 3:
                v.u64 = htole64(value);
                break;
        case 2:
                v.u32 = htole32(value);
                break;
        case 1:
                v.u16 = htole16(value);
                break;
        case 0:
                v.u8 = value;
                break;
        }

        memcpy(addr, &v, 1 << wordsize);
}
#endif

/*
 * Levels
 * ======
 *
 * GVariants have two types of nesting: native nesting and recursion. Recursion
 * is implemented via the 'v' type and allows storing a blob with its entire
 * type-string. This way, arbitrary (but introspectable) data can be embedded
 * in a GVariant.
 * Native nesting, on the other hand, allows building complex types out of
 * basic types. This includes tuples, arrays, maybes, ... and allows to build
 * compound types.
 *
 * Preferably, accessor functions would return compound types in one batch.
 * However, while some of them can be repesented natively in C, some cannot
 * (especially non-static types). Therefore, we provide iterators to access
 * each nesting level individually.
 *
 * Internally, we remember state for each level we enter, and drop the state
 * when exiting it. The state is stored in an array of CVariantLevel objects.
 * Each CVariant can contains a static set of inline levels up to
 * C_VARIANT_MAX_INLINE_LEVELS. Once a deeper nesting level is reached, a new
 * set of levels must be allocated. This is handled by the CVariant management
 * code, though. The CVariantLevel code only deas with each level individually.
 *
 * The state on each level consists of a set of static fields:
 *  - size: size in bytes available to this level
 *  - i_end, v_end: offset into data vectors, pointing *after* the last byte
 *                  of this level
 *  - wordsize: cached wordsize of this level
 *  - enclsing: type of the enclosing container
 *
 * and also of a set of dynamic fields:
 *  - i_type, n_type: index into the type signature pointing at the next type
 *                    to be parsed (updated together with iterator)
 *  - index: container specific state
 *  - offset: offset in bytes between the start of the container and the
 *            current iterator (can be calculated with i_end/v_end,
 *            i_front/v_front, and size, but rather expensive)
 *  - i_type, v_type: offset into data vectors, pointer *at* the current
 *                    iterator position
 *
 * The @index field is special, as it depends on the enclosing container:
 *  - In case of arrays, @index contains the number of remaining array elements
 *    to be parsed. 0 means end of array.
 *  - In the case of tuples, @index counts the number of *already* parsed
 *    dynamic-sized children *plus 1* (i.e., '@index - 1' is what you want).
 *  - In the case of variants, @index is the offset of the start of the variant
 *    type, which is always appended at the end to a variant.
 *  - In the case of maybes, @index is 1 if the maybe is non-empty, 0 if it is
 *    'Nothing'.
 *  - In all other cases, @index is always 1.
 *
 * Note that we support iovecs as underlying data source. Data is not required
 * to be mapped linearly. This, however, forces us to use a tuple as iterator
 * (vector index *and* offset into the vector), rather than a simple single
 * offset into the buffer. Otherwise, we would have to iterate all iovecs
 * everytime we index it by a single offset.
 * Using the tuple as iterator allows us to index a vector directly, by simply
 * accessing the correct iovec (based on the vector index) and then the offset
 * (based on the data offset).
 * As an optimization, we perform most jumps/movements on the data offset
 * directly. This means, if we hit a boundary between two vectors, we do *not*
 * increment the vector index. Instead, we delay this until we actually
 * dereference the iterator. We call this operation 'folding', as we fold the
 * data offset into the iovec array. We also support the reverse operation
 * 'unfolding', which is very handy if iterators are dereferenced with
 * temporary offsets all the time.
 */

static_assert(sizeof(CVariantLevel) <= sizeof(size_t) * 8,
              "Invalid structure packing");
static_assert((1ULL << (8 * sizeof(((CVariantLevel *)0)->i_type))) > C_VARIANT_MAX_SIGNATURE,
              "Invalid maximum signature length");
static_assert((1ULL << (8 * sizeof(((CVariantLevel *)0)->n_type))) > C_VARIANT_MAX_SIGNATURE,
              "Invalid maximum signature length");

static void c_variant_level_root(CVariantLevel *level, CVariant *cv, size_t size) {
        /*
         * Initialize the root-level to occupy @size bytes of the available
         * space. The caller should usually have calculated it based on the set
         * of iovecs available.
         */

        level->size = size;
        level->v_end = 0;
        level->i_end = size;
        level->wordsize = c_variant_word_size(size, 0);
        level->enclosing = C_VARIANT_TUPLE_OPEN;

        level->i_type = 0;
        level->n_type = cv->n_type;
        level->offset = 0;
        level->i_front = 0;
        level->v_front = 0;

        /*
         * For non-arrays, the index is the number of successfully parsed
         * dynamic size objects, plus 1. As we haven't parsed any dynamic sized
         * object, yet, set it to the initial 1.
         */
        level->index = 1;
}

static void c_variant_level_trim(CVariantLevel *level, size_t size) {
        /*
         * Assuming that @level is a copy of the parent level, this function
         * trims the size of @level to @size bytes. While at it, it resets the
         * index and offset to the beginning of the level. Additionally, the
         * wordsize is re-calculated and cached.
         */

        level->index = 0;
        level->offset = 0;
        level->size = size;
        level->wordsize = c_variant_word_size(size, 0);

        level->v_end = level->v_front;
        level->i_end = level->i_front + size;
}

static void c_variant_level_align(CVariantLevel *level, size_t alignment) {
        size_t offset;

        /*
         * This function aligns the front pointer according to the alignment
         * given as @alignment (power of 2). This adjusts both, the front index
         * *and* the global offset counter.
         *
         * Note that @i_front does not reflect actual global alignment. Vectors
         * might be split arbitrarily. We have to use @offset to calculate the
         * required padding for the given alignment. We know that each
         * container is properly aligned to its maximum alignment, so @offset
         * is as well (given that 0 means 'start of container').
         */

        offset = ALIGN_TO(level->offset, 1 << alignment);
        level->i_front += offset - level->offset;
        level->offset = offset;
}

static void c_variant_level_jump(CVariant *cv, CVariantLevel *level, size_t offset) {
        size_t diff;

        /*
         * This moves the current front-iterator to the specified offset,
         * relative to the start of the container.
         *
         * Preferably, we would treat @i_front/@i_end as signed indices and
         * only dereference/fold them if [offset < size]. But, unfortunately,
         * signed-overflow is undefined in C (hurray!), so we cannot do that.
         * We could rather convert everything to unsigned, but that makes the
         * code hardly readable. Instead, we just fold @i_front directly on
         * each negative jump (which only occur in non-canonical data, anyway).
         */

        if (_likely_(offset >= level->offset)) {
                level->i_front += offset - level->offset;
        } else {
                diff = level->offset - offset;

                while (diff > level->i_front) {
                        assert(level->v_front > 0);

                        diff -= level->i_front;
                        level->i_front = cv->vecs[--level->v_front].iov_len;
                }

                level->i_front -= diff;
        }

        level->offset = offset;
}

static void *c_variant_level_front(CVariant *cv, CVariantLevel *level, size_t *sizep) {
        size_t size = 0;
        void *p = NULL;

        /*
         * This folds @v_front[@i_front] onto the existing vecs of @cv and then
         * returns a pointer to the current location, together with the maximum
         * size that can be accessed linearly at this position. Note that this
         * size may be 0, especially if the current position is outside the
         * parent container.
         */

        if (level->offset < level->size) {
                struct iovec *v = cv->vecs + level->v_front;

                /* fold front, if it advanced compared to previous call */
                while (level->i_front >= v->iov_len) {
                        assert(level->v_front + 1 < cv->n_vecs);

                        level->i_front -= v->iov_len;
                        ++level->v_front;
                        ++v;
                }

                size = v->iov_len - level->i_front;
                if (size > level->size - level->offset)
                        size = level->size - level->offset;

                p = (char *)v->iov_base + level->i_front;
        }

        *sizep = size;
        return p;
}

static void *c_variant_level_tail(CVariant *cv, CVariantLevel *level, size_t skip, size_t *sizep) {
        struct iovec *v;
        size_t size = 0;
        void *p = NULL;

        /*
         * This is similar to c_variant_level_front(), but maps the tail of
         * this level. Furthermore, since the tail is fixed and cannot be
         * moved, this function accepts an additional @skip count, that
         * specifies a negative offset relative to the end of the level, which
         * is skipped prior to mapping the tail.
         * Note that this maps _backwards_. So first @skip is subtracted from
         * the current tail, then as much space as possible is mapped
         * *backwards* starting at the new tail. The returned pointer points to
         * the start of the tail, and the size contains the maximum linearly
         * accessible memory at this position, which is still inside the level.
         *
         * The end-pointer of a level is static. We cannot modify it. Hence, we
         * perform unfolding, based on @skip, assuming that tail accesses do
         * not jump randomly.
         */

        if (skip < level->size) {
                /* unfold tail, if @skip increased compared to prev call */
                while (skip >= level->i_end) {
                        assert(level->v_end > 0);
                        level->i_end += cv->vecs[--level->v_end].iov_len;
                }

                /* fold tail, if @skip decreased compared to prev call */
                v = cv->vecs + level->v_end;
                while (level->i_end - skip > v->iov_len) {
                        assert(level->v_end + 1 < cv->n_vecs);

                        level->i_end -= v->iov_len;
                        ++level->v_end;
                        ++v;
                }

                if (level->size < level->i_end)
                        size = level->size - skip;
                else
                        size = level->i_end - skip;

                p = (char *)v->iov_base + level->i_end - skip - size;
        }

        *sizep = size;
        return p;
}

/*
 * Variants
 * ========
 *
 * Each CVariant object represents a single type (basic _or_ compound) and
 * contains an array of CVariantLevel objects for state tracking in compound
 * types. However, the GVariant specification allows true recursion via the 'v'
 * type. Such a recursion cannot be represented via a CVariantLevel, but
 * instead requires a separat CVariant to be allocated. Hence, whenever you
 * enter a 'v' type, we transparently allocate a separate CVariant object and
 * switch to it. The same operation is done whenever you enter hierarchies
 * deeper than the allocate level-stack.
 *
 * This feature allows us to extract *any* child-variant of compound types or
 * 'v' types into a new, independent CVariant object, rather than just pushing
 * another CVariantLevel. While it is optional for compound types, this is
 * mandated for 'v' types.
 *
 * However, we really do not want API users to have to deal with that, if they
 * do not want to. Therefore, each CVariant contains its own list of stacked
 * CVariants that we entered (via the @parent pointer). Whenever a CVariant is
 * exited into its parent, we remember the old one in @unused so we can reuse
 * it later on.
 * Preferably, whenever you enter the next CVariant, we'd just switch your
 * CVariant pointer to point to the new one. But that is hardly possible and
 * would constrain the API a lot. What we do instead, is we copy the current
 * CVariant state contents into the next CVariant and link that one as our
 * parent instead. It is a gross hack, but reasonable confined and hidden in
 * two helper functions that push/pop levels.
 */

static_assert(__alignof(struct iovec) <= __alignof(CVariant),
              "Invalid CVariant alignment");
static_assert(__alignof(CVariantLevel) <= __alignof(CVariant),
              "Invalid CVariant alignment");
static_assert((1ULL << (8 * sizeof(((CVariant *)0)->n_type))) > C_VARIANT_MAX_SIGNATURE,
              "Invalid maximum signature length");
static_assert((1ULL << (8 * sizeof(((CVariant *)0)->i_levels))) > C_VARIANT_MAX_LEVEL,
              "Invalid maximum depth level");
static_assert((1ULL << (8 * sizeof(((CVariant *)0)->n_vecs))) > UIO_MAXIOV,
              "Invalid maximum number of iovecs");

static int c_variant_init_root(CVariant *cv, CVariantType *type) {
        size_t i, size;

        /*
         * You'd assume that if all iovecs are mapped in memory, an overflow in
         * 'size' could not happen. However, in case the mappings overlap, it
         * can. Hence, we must verify that the actual total size can be
         * represented in a single 'size_t'. We do not support reading variants
         * bigger than our native word. If you need this, do it yourself..
         */
        size = 0;
        for (i = 0; i < cv->n_vecs; ++i) {
                if (size + cv->vecs[i].iov_len < size)
                        return -EFBIG;
                size += cv->vecs[i].iov_len;
        }

        /*
         * So this is a bit questionable: If you create a new root level object
         * of a fixed-size type, you better use a buffer of the exact size.
         * Otherwise, you really do something stupid. So we should do this:
         *
         *     if (type->size > 0 && type->size != size)
         *             size = 0;
         *
         * However, imagine a deeply nested, complex, compound type of static
         * size. If you receive such a type from a remote party and want to
         * parse it, you actually *want* to make use of the defined
         * error-handling of GVariant, just as if it was a child of a
         * dynamic-sized object. Hence, we accept any size here.
         */

        c_variant_level_root(cv->levels, cv, size);
        return 0;
}

static int c_variant_alloc(CVariant **out,
                           size_t n_type,
                           size_t n_vecs,
                           size_t n_levels) {
        size_t size, off_vecs, off_levels;
        CVariant *cv;

        /*
         * Allocate a new CVariant object, and reserve enough space for a type
         * of length @n_type, a vector array of length @n_vecs, and a level
         * array of @n_levels levels. All is stored properly aligned in the
         * appendix.
         */

        /* cv->levels[cv->i_levels] better always works! */
        assert(n_levels > 0);

        if (n_levels > C_VARIANT_MAX_INLINE_LEVELS)
                n_levels = C_VARIANT_MAX_INLINE_LEVELS;

        size = offsetof(CVariant, appendix);
        size += n_type;
        if (n_vecs > 0) {
                off_vecs = ALIGN_TO(size, __alignof(struct iovec));
                size = off_vecs + n_vecs * sizeof(struct iovec);
        }
        if (n_levels > 0) {
                off_levels = ALIGN_TO(size, __alignof(CVariantLevel));
                size = off_levels + n_levels * sizeof(CVariantLevel);
        }

        cv = memalign(__alignof(CVariant), size);
        if (!cv)
                return -ENOMEM;

        memset(cv, 0, offsetof(CVariant, appendix));
        if (n_type > 0) {
                cv->type = (void *)cv->appendix;
                cv->n_type = n_type;
        }
        if (n_vecs > 0) {
                cv->vecs = (void *)((char *)cv + off_vecs);
                cv->n_vecs = n_vecs;
        }
        if (n_levels > 0) {
                cv->levels = (void *)((char *)cv + off_levels);
                cv->n_levels = n_levels;
        }

        *out = cv;
        return 0;
}

static int c_variant_poison_internal(CVariant *cv, int poison) {
        if (cv->poison)
                return -cv->poison;

        cv->poison = -poison;
        return -cv->poison;
}

#define c_variant_poison(_cv, _poison)                          \
        ({                                                      \
                static_assert((_poison) < 0,                    \
                              "Invalid poison code");           \
                static_assert(-(_poison) < (1LL << 7),          \
                              "Invalid poison code");           \
                c_variant_poison_internal((_cv), (_poison));    \
        })

static int c_variant_push_level(CVariant *cv, char element, CVariantLevel **out) {
        CVariantLevel *levels;
        CVariant *parent, *unused;
        size_t n_levels;
        int r;

        if (element != C_VARIANT_VARIANT && _likely_(cv->i_levels + 1 < cv->n_levels)) {
                *out = &cv->levels[++cv->i_levels];
                return 0;
        }

        if (cv->unused) {
                parent = cv->unused;
                cv->unused = NULL;
        } else {
                r = c_variant_alloc(&parent, 0, 0, C_VARIANT_MAX_INLINE_LEVELS);
                if (r < 0)
                        return r;
        }

        unused = parent->unused;
        levels = parent->levels;
        n_levels = parent->n_levels;

        memcpy(parent, cv, offsetof(CVariant, appendix));

        cv->parent = parent;
        cv->unused = unused;
        cv->levels = levels;
        cv->i_levels = 0;
        cv->n_levels = n_levels;

        *out = levels;
        return 0;
}

static bool c_variant_pop_level(CVariant *cv) {
        CVariantLevel *levels;
        CVariant *parent, *unused;
        int poison;

        if (_likely_(cv->i_levels > 0)) {
                --cv->i_levels;
                return true;
        } else if (cv->parent) {
                /* you better did not modify one of those fields */
                assert(cv->vecs == cv->parent->vecs);
                assert(cv->n_vecs == cv->parent->n_vecs);
                assert(cv->sealed == cv->parent->sealed);

                parent = cv->parent;
                unused = cv->unused;
                levels = cv->levels;
                poison = cv->poison;

                memcpy(cv, parent, offsetof(CVariant, appendix));

                parent->levels = levels;
                parent->unused = unused;
                cv->unused = parent;
                cv->poison = poison;
                return true;
        }

        return false;
}

/*
 * Readers
 * =======
 *
 * Once a variant is sealed, you can use the reader-functions to deserialize a
 * variant from memory. All these readers follow the GVariant spec closely and
 * should be rather straightforward.
 */

static int c_variant_peek(CVariant *cv,
                          CVariantLevel *level,
                          char element,
                          CVariantType *infop,
                          size_t *sizep,
                          size_t *endp,
                          void **frontp) {
        size_t offset;
        int r;

        /*
         * Take a peek at the current iterator position and return any required
         * information to parse the next type. The caller has to provide the
         * element it expects next via @element.
         *
         * The information returned includes:
         *  - type information, in @infop
         *  - size available to the type, in @sizep
         *  - the offset where the element ends, in @endp
         *  - if non-NULL, pointer to the linear memory of the type, in @front
         *
         * Note that if the container does not fulfill the requirements of the
         * next element, @sizep might be truncated to 0, which implies that
         * the default value should be used instead. Furthermore, if @frontp
         * is non-NULL, but the type is not accessible via linear memory, NULL
         * will be returned as front pointer.
         */

        if (_unlikely_(level->n_type < 1 ||
                       cv->type[level->i_type] != element ||
                       level->index == 0))
                return c_variant_poison(cv, -EBADRQC);

        /* retrieve full type information and align front accordingly */
        r = c_variant_signature_next(cv->type + level->i_type, level->n_type,
                                     infop);
        assert(r == 1);

        c_variant_level_align(level, infop->alignment);
        offset = level->offset;

        /* now figure out the size provided to the type */
        if (infop->size > 0) { /* fixed-size type */
                offset += infop->size;
        } else { /* dynamic-size type */
                size_t tail_size, idx, wz;
                void *tail;

                wz = 1 << level->wordsize;

                switch (level->enclosing) {
                case C_VARIANT_VARIANT:
                        offset = level->index - 1;
                        break;
                case C_VARIANT_MAYBE:
                        offset = level->size - 1;
                        break;
                case C_VARIANT_ARRAY:
                        idx = (level->index - 1) * wz;
                        tail = c_variant_level_tail(cv, level, idx, &tail_size);

                        if (_likely_(wz <= tail_size)) {
                                tail = (char *)tail + tail_size - wz;
                                offset = c_variant_word_fetch(tail, level->wordsize);
                        }
                        break;
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        if (infop->n_type == level->n_type) {
                                offset = level->size;
                        } else {
                                idx = (level->index - 1) * wz;
                                tail = c_variant_level_tail(cv, level, idx, &tail_size);

                                if (_likely_(wz <= tail_size)) {
                                        tail = (char *)tail + tail_size - wz;
                                        offset = c_variant_word_fetch(tail, level->wordsize);
                                }
                        }
                        break;
                default:
                        assert(0);
                        c_variant_poison(cv, -EFAULT);
                        /* fallthrough */
                }
        }

        /* always provide end-offset to caller */
        *endp = offset;

        /* truncate type, if the provided frame exceeds the container */
        if (_likely_(offset >= level->offset && offset <= level->size))
                *sizep = offset - level->offset;
        else
                *sizep = 0;

        /* measure front, if front information is requested */
        if (frontp) {
                size_t front_size;
                void *front;

                front = c_variant_level_front(cv, level, &front_size);
                *frontp = (*sizep <= front_size) ? front : NULL;
        }

        return 0;
}

static void c_variant_advance(CVariant *cv, CVariantLevel *level, CVariantType *info, size_t end) {
        /* jump to end of type */
        c_variant_level_jump(cv, level, end);

        /* adjust index and/or type */
        switch (level->enclosing) {
        case C_VARIANT_ARRAY:
        case C_VARIANT_MAYBE:
                --level->index;
                break;
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                if (info->size == 0)
                        ++level->index;
                /* fallthrough */
        default:
                level->i_type += info->n_type;
                level->n_type -= info->n_type;
                break;
        }
}

static int c_variant_enter_one(CVariant *cv, char container) {
        CVariantLevel *next, *level;
        CVariantType info;
        size_t size, end;
        int r;

        /* remember current level *before* pushing @next */
        level = cv->levels + cv->i_levels;

        r = c_variant_peek(cv, level, container, &info, &size, &end, NULL);
        if (r < 0)
                return r;

        r = c_variant_push_level(cv, container, &next);
        if (r < 0)
                return r;

        *next = *level;
        next->enclosing = container;
        next->i_type += 1;
        next->n_type = info.n_type - 1;
        c_variant_level_trim(next, size);

        switch (container) {
        case C_VARIANT_VARIANT: {
                CVariantType child;
                size_t tail_size, i;
                char *tail;

                tail = c_variant_level_tail(cv, next, 0, &tail_size);
                for (i = 1; i < tail_size; ++i)
                        if (tail[tail_size - i - 1] == 0)
                                break;
                if (i < tail_size) {
                        r = c_variant_signature_one(tail + tail_size - i, i, &child);
                        if (r >= 0) {
                                cv->type = tail + tail_size - i;
                                cv->n_type = i;
                                next->index = next->size - i;
                        }
                }
                if (next->index == 0) {
                        cv->type = "()";
                        cv->n_type = 2;
                        next->index = 1;
                }
                next->i_type = 0;
                next->n_type = cv->n_type;
                break;
        }
        case C_VARIANT_MAYBE:
                if (size > 0 && (info.bound_size == 0 || info.bound_size == size))
                        next->index = 1;
                break;
        case C_VARIANT_ARRAY:
                if (info.bound_size > 0) { /* fixed-size array */
                        if (_likely_(size % info.bound_size == 0))
                                next->index = size / info.bound_size;
                } else { /* dynamic-size array */
                        size_t tail_size, offset, num, wz;
                        void *tail;

                        tail = c_variant_level_tail(cv, next, 0, &tail_size);
                        wz = 1 << next->wordsize;

                        if (_likely_(wz <= tail_size)) {
                                tail = (char *)tail + tail_size - wz;
                                offset = c_variant_word_fetch(tail, next->wordsize);
                                num = size - offset;
                                if (_likely_(offset < size && num % wz == 0))
                                        next->index = num / wz;
                        }
                }
                break;
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                --next->n_type; /* drop closing bracket */
                next->index = 1;
                break;
        default:
                assert(0);
                c_variant_pop_level(cv);
                return c_variant_poison(cv, -EFAULT);
        }

        c_variant_advance(cv, level, &info, end);
        return 0;
}

static int c_variant_exit_one(CVariant *cv) {
        if (!c_variant_pop_level(cv))
                return c_variant_poison(cv, -EBADRQC);

        return 0;
}

static int c_variant_exit_try(CVariant *cv, char container) {
        if (container != cv->levels[cv->i_levels].enclosing)
                return c_variant_poison(cv, -EBADRQC);

        return c_variant_exit_one(cv);
}

static int c_variant_read_one(CVariant *cv, CVariantLevel *level, char basic, void *arg) {
        static const char default_value[8] = {};
        CVariantType info;
        size_t size, end;
        void *front;
        int r;

        r = c_variant_peek(cv, level, basic, &info, &size, &end, &front);
        if (r < 0)
                return r;

        switch (basic) {
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
                if (arg)
                        memcpy(arg, front ?: default_value, size);
                break;
        case C_VARIANT_STRING:
        case C_VARIANT_PATH:
        case C_VARIANT_SIGNATURE:
                if (arg) {
                        if (!front || size == 0 || ((char *)front)[size - 1])
                                front = NULL;
                        *(const void **)arg = front ?: default_value;
                }
                break;
        default:
                assert(0);
                return c_variant_poison(cv, -EFAULT);
        }

        c_variant_advance(cv, level, &info, end);
        return 0;
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

static int c_variant_varg_next(CVariantVarg *varg) {
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

static char c_variant_varg_init(CVariantVarg *varg, const char *type, size_t n_type) {
        varg->i_levels = 0;
        varg->levels[0].type = type;
        varg->levels[0].n_type = n_type;
        varg->levels[0].n_array = -1;
        return c_variant_varg_next(varg);
}

static void c_variant_varg_push(CVariantVarg *varg, const char *type, size_t n_type, size_t n_array) {
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

static void c_variant_varg_enter_bound(CVariantVarg *varg, CVariant *cv, size_t n_array) {
        CVariantVargLevel *vlevel = &varg->levels[varg->i_levels];
        CVariantLevel *level = cv->levels + cv->i_levels;

        /* callers better know the type before accessing it */
        assert(vlevel->n_type >= level->n_type);
        assert(!strncmp(vlevel->type, cv->type + level->i_type, level->n_type));

        c_variant_varg_push(varg, vlevel->type, level->n_type, n_array);
        vlevel->type += level->n_type;
        vlevel->n_type -= level->n_type;
}

static void c_variant_varg_enter_unbound(CVariantVarg *varg, CVariant *cv, char closing) {
        CVariantVargLevel *vlevel = &varg->levels[varg->i_levels];
        CVariantLevel *level = cv->levels + cv->i_levels;

        /* callers better know the type before accessing it */
        assert(vlevel->n_type >= level->n_type + 1U);
        assert(!strncmp(vlevel->type, cv->type + level->i_type, level->n_type));
        assert(vlevel->type[level->n_type] == closing);

        c_variant_varg_push(varg, vlevel->type, level->n_type, -1);
        vlevel->type += level->n_type + 1U;
        vlevel->n_type -= level->n_type + 1U;
}

static void c_variant_varg_enter_default(CVariantVarg *varg, bool is_bound, size_t n_array) {
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
 * c_variant_new_from_vecs() - create new variant from given type and blob
 * @cvp:        output variable for new variant
 * @type:       type string
 * @n_type:     length of @type
 * @vecs:       data vectors
 * @n_vecs:     number of vectors in @vecs
 *
 * This allocates a new variant of type @type, pointing to the serialized
 * variant in @vecs. The new variant is already sealed and ready for
 * deserialization.
 *
 * The iovec-array is copied into the variant itself. The caller is free to
 * release it at any time. The underlying data is *NOT* copied, though. It must
 * stay accessible for the entire lifetime of the variant.
 *
 * The type string is copied into the variant and can be released by the
 * caller.
 *
 * On success, the new variant is returned in @cvp. On failure, @cvp stays
 * untouched.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_new_from_vecs(CVariant **cvp,
                                     const char *type,
                                     size_t n_type,
                                     const struct iovec *vecs,
                                     size_t n_vecs) {
        CVariantType info;
        CVariant *cv;
        int r;

        assert(type || n_type == 0);
        assert(vecs || n_vecs == 0);

        r = c_variant_signature_one(type, n_type, &info);
        if (r < 0)
                return r;

        r = c_variant_alloc(&cv, n_type, n_vecs, info.n_levels + 1);
        if (r < 0)
                return r;

        memcpy((void *)cv->type, type, n_type);
        memcpy(cv->vecs, vecs, sizeof(*vecs) * n_vecs);
        cv->sealed = true;

        r = c_variant_init_root(cv, &info);
        if (r < 0)
                goto error;

        *cvp = cv;
        return 0;

error:
        c_variant_free(cv);
        return r;
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
        CVariant *u, *v;

        if (cv) {
                u = cv->unused;
                while (u) {
                        v = u;
                        u = u->unused;
                        free(v);
                }

                u = cv->parent;
                while (u) {
                        v = u;
                        u = u->parent;
                        free(v);
                }

                free(cv);
        }

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
 * c_variant_peek_count() - return the number of dynamic elements left
 * @cv:         variant to operate on, or NULL
 *
 * This function looks at the current iterator position and calculates the
 * numbers of _dynamic_ elements left to read, until the end of the container.
 * The returned number depends on the current container type:
 *  - In arrays, it returns the number of array entries left, or 0 if at the
 *    end of the array.
 *  - In maybes, it returns 0 if the maybe is 'Nothing', or if the entry was
 *    already read. Otherwise, 1 is returned.
 *  - In all other containers, it returns 1 if there are _any_ types left to
 *    read, or 0 if at the end of the container.
 *
 * In other words, if this function returns non-zero, then you can read some
 * data via c_variant_read(). If it returns zero, there is nothing left to read
 * on this container level.
 *
 * It is an programming error to call this on an unsealed variant.
 *
 * Return: Number of dynamic elements left to read.
 */
_public_ size_t c_variant_peek_count(CVariant *cv) {
        CVariantLevel *level;

        if (_unlikely_(!cv))
                return 1; /* type: "()" */

        assert(cv->sealed);

        level = cv->levels + cv->i_levels;
        switch (level->enclosing) {
        case C_VARIANT_ARRAY:
                return level->index;
        case C_VARIANT_MAYBE:
                return level->index;
        default:
                return level->n_type > 0;
        }
}

/**
 * c_variant_peek_type() - peek at the type string ahead
 * @cv:         variant to operate on, or NULL
 * @sizep:      output variable to store type-string length to
 *
 * This returns a pointer to the type-string ahead in the current container.
 * That is, it returns the types that can be read via c_variant_read() from the
 * current position, without any call to c_variant_exit().
 *
 * CAREFUL: The type-string is *NOT* zero-terminated, as it might very well
 *          point right into the middle of some nested container. You *MUST*
 *          provide @sizep and use this to limit the type-string in length.
 *
 * It is an programming error to call this on an unsealed variant.
 *
 * Return: Pointer to type string is returned.
 */
_public_ const char *c_variant_peek_type(CVariant *cv, size_t *sizep) {
        CVariantLevel *level;

        if (_unlikely_(!cv)) {
                *sizep = 2;
                return "()";
        }

        assert(cv->sealed);

        level = cv->levels + cv->i_levels;
        *sizep = level->n_type;
        return cv->type + level->i_type;
}

/**
 * c_variant_enter() - enter container
 * @cv:         variant to operate on, or NULL
 * @containers: containers to enter, or NULL
 *
 * This moves the current iterator of @cv into the containers ahead. If
 * @containers is NULL, this function simply enters the next container ahead.
 * Otherwise, @containers must be string consisting of the valid container
 * types. This function then consequetively enters each specified container, as
 * if you called c_variant_enter() multiple times for each type.
 *
 * Valid elements for @containers are:
 *   'v' to enter a variant
 *   'm' to enter a maybe
 *   'a' to enter an array
 *   '(' to enter a tuple
 *   '{' to enter a pair
 *
 * If the type ahead is not a container, or does not match the specified
 * container type, this function will stop the operation at this point and
 * return an error.
 *
 * It is an programming error to call this on an unsealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_enter(CVariant *cv, const char *containers) {
        CVariantLevel *level;
        int r;

        if (_unlikely_(!cv))
                return -ENOTUNIQ;

        assert(cv->sealed);

        if (containers) {
                for ( ; *containers; ++containers) {
                        switch (*containers) {
                        case C_VARIANT_VARIANT:
                        case C_VARIANT_MAYBE:
                        case C_VARIANT_ARRAY:
                        case C_VARIANT_TUPLE_OPEN:
                        case C_VARIANT_PAIR_OPEN:
                                r = c_variant_enter_one(cv, *containers);
                                if (r < 0)
                                        return r;
                                break;
                        default:
                                return c_variant_poison(cv, -EMEDIUMTYPE);
                        }
                }
        } else {
                level = cv->levels + cv->i_levels;
                if (level->i_type >= cv->n_type)
                        return c_variant_poison(cv, -EBADRQC);

                r = c_variant_enter_one(cv, cv->type[level->i_type]);
                if (r < 0)
                        return r;
        }

        return 0;
}

/**
 * c_variant_exit() - exit container
 * @cv:         variant to operate on, or NULL
 * @containers: containers to exit, or NULL
 *
 * This function is the counter-part to c_variant_enter() (see its
 * documentation for details). It works very similar, but rather than entering
 * a container, it exits them and returns to their parent container.
 *
 * Valid elements for @containers are:
 *   'v' to exit a variant
 *   'm' to exit a maybe
 *   'a' to exit an array
 *   ')' to exit a tuple
 *   '}' to exit a pair
 *
 * It is an programming error to call this on an unsealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_exit(CVariant *cv, const char *containers) {
        char enclosing;
        int r;

        if (_unlikely_(!cv))
                return -ENOTUNIQ;

        assert(cv->sealed);

        if (!containers)
                return c_variant_exit_one(cv);

        for ( ; *containers; ++containers) {
                switch (*containers) {
                case C_VARIANT_VARIANT:
                case C_VARIANT_MAYBE:
                case C_VARIANT_ARRAY:
                        enclosing = *containers;
                        break;
                case C_VARIANT_TUPLE_CLOSE:
                        enclosing = C_VARIANT_TUPLE_OPEN;
                        break;
                case C_VARIANT_PAIR_CLOSE:
                        enclosing = C_VARIANT_PAIR_OPEN;
                        break;
                default:
                        return c_variant_poison(cv, -EMEDIUMTYPE);
                }

                r = c_variant_exit_try(cv, enclosing);
                if (r < 0)
                        return r;
        }

        return 0;
}

static void c_variant_readv_default(CVariantVarg *varg, int c, va_list args) {
        static const char default_value[8] = {};
        const char *arg_s;
        size_t size;
        void *arg;
        int arg_i;

        /*
         * This is the fallback readv() implementation. It does not require
         * access to any variant, but rather just fills in default values for
         * all requested elements. This makes sure that even if readv() fails,
         * all output variables will have a valid value.
         */

        static_assert(sizeof(double) == 8, "Unsupported 'double' type");

        for ( ; c; c = c_variant_varg_next(varg)) {
                size = 1;

                switch (c) {
                case -1: /* level done, nothing to do */
                        break;
                case C_VARIANT_INT64:
                case C_VARIANT_UINT64:
                case C_VARIANT_DOUBLE:
                        size *= 2; /* fallthrough */
                case C_VARIANT_INT32:
                case C_VARIANT_UINT32:
                case C_VARIANT_HANDLE:
                        size *= 2; /* fallthrough */
                case C_VARIANT_INT16:
                case C_VARIANT_UINT16:
                        size *= 2; /* fallthrough */
                case C_VARIANT_BOOL:
                case C_VARIANT_BYTE:
                        arg = va_arg(args, void *);
                        if (arg)
                                memcpy(arg, default_value, size);
                        break;
                case C_VARIANT_STRING:
                case C_VARIANT_PATH:
                case C_VARIANT_SIGNATURE:
                        arg = va_arg(args, void *);
                        if (arg)
                                *(const char **)arg = default_value;
                        break;
                case C_VARIANT_VARIANT:
                        arg_s = va_arg(args, const char *);
                        if (arg_s)
                                c_variant_varg_push(varg, arg_s, strlen(arg_s), -1);
                        break;
                case C_VARIANT_MAYBE:
                case C_VARIANT_ARRAY:
                        /* bool in varargs becomes int */
                        arg_i = va_arg(args, int);
                        c_variant_varg_enter_default(varg, true, arg_i);
                        break;
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        c_variant_varg_enter_default(varg, false, -1);
                        break;
                default:
                        assert(0);
                        break;
                }
        }
}

/**
 * c_variant_readv() - read data from variant
 * @cv:         variant to operate on, or NULL
 * @signature:  signature string
 * @args:       additional parameters
 *
 * This advances the internal iterator of @cv according to the signature string
 * @signature, deserializing each type and returning the information to the
 * caller.
 *
 * This function reads one type after another from the signature, until it hits
 * the end of the signature. For each type, the following operation is done:
 *   - basic types: For any basic type, the caller must provide a pointer to
 *                  the type in @args. This function will copy the deserialized
 *                  data into the provided variable. If NULL is passed, the
 *                  data is not copied.
 *                  In case of strings, this function only copies over a
 *                  pointer to the string. That is, you need to have a 'char*'
 *                  on the stack, and pass it as 'char**' to this function,
 *                  which will update the pointer. Note that *any* returned
 *                  string is *always* 0 terminated!
 *  - variants: For every variant you specify, you must provide a type string
 *              in @args. If it is NULL, the variant is simply skipped.
 *              Otherwise, the variant is entered (expecting it to have the
 *              specified type) and each value is read recursively.
 *  - maybe: For every maybe you specify, you must provide a boolean in @args
 *           that specifies whether you expect the maybe to be empty (false),
 *           or valid (true). If it is valid, this will enter the maybe and
 *           reads its values recursively.
 *  - array: For every array you specify, you must provide an signed integer in
 *           @args that tells how many array elements you want to read. If
 *           non-zero, this will enter the array-type and recursively read each
 *           value.
 *  - tuple/pair: Tuples and pairs are simply entered/exited as specified.
 *                Their values and then read recursively, according to their
 *                type.
 *
 * Whenever you specify a type that does *NOT* match the actual type of the
 * variant, an error will be returned. However, any remaining types in @args
 * (according to the data provided in @signature) are read as their default
 * values. That is, even if this function fails, all output arguments will have
 * *some* valid data!
 *
 * Example:
 *      unsigned int u1, u2, u3, u4;
 *      const char *key1, *key2;
 *
 *      r = c_variant_read(cv, "(uua(sv))",
 *                         &u1,         // read first 'u'
 *                         &u2,         // read second 'u'
 *                         2,           // read 2 array entries
 *                         &key1,       // read 's' of first array entry
 *                         "mu",        // type of 'v' of first array entry
 *                         true,        // expect maybe to be non-empty
 *                         &u4,         // read 'mu' of first array entry
 *                         &key2,       // read 's' of second array entry
 *                         "u",         // type of 'v' of second array entry
 *                         &u4);        // read 'u' of second array entry
 *
 * It is an programming error to call this on an unsealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_readv(CVariant *cv, const char *signature, va_list args) {
        CVariantVarg varg;
        const char *arg_s;
        void *arg;
        int arg_i;
        int r, c;

        if (_unlikely_(!signature || !*signature))
                return 0;

        c = c_variant_varg_init(&varg, signature, strlen(signature));

        if (_unlikely_(!cv)) {
                if (!strcmp(signature, "()"))
                        return 0;
                c_variant_readv_default(&varg, c, args);
                return -EBADRQC;
        }

        assert(cv->sealed);

        for ( ; c; c = c_variant_varg_next(&varg)) {
                switch (c) {
                case -1: /* level done */
                        c_variant_exit_one(cv);
                        break;
                case C_VARIANT_VARIANT:
                        r = c_variant_enter_one(cv, c);
                        if (r < 0) {
                                c_variant_readv_default(&varg, c, args);
                                return r;
                        }

                        arg_s = va_arg(args, const char *);
                        if (arg_s)
                                c_variant_varg_push(&varg, arg_s, strlen(arg_s), -1);
                        else
                                c_variant_exit_one(cv);
                        break;
                case C_VARIANT_MAYBE:
                case C_VARIANT_ARRAY:
                        r = c_variant_enter_one(cv, c);
                        if (r < 0) {
                                c_variant_readv_default(&varg, c, args);
                                return r;
                        }

                        /* bool in varargs becomes int */
                        arg_i = va_arg(args, int);
                        c_variant_varg_enter_bound(&varg, cv, arg_i);
                        break;
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        r = c_variant_enter_one(cv, c);
                        if (r < 0) {
                                c_variant_readv_default(&varg, c, args);
                                return r;
                        }

                        if (c == C_VARIANT_TUPLE_OPEN)
                                c_variant_varg_enter_unbound(&varg, cv, C_VARIANT_TUPLE_CLOSE);
                        else
                                c_variant_varg_enter_unbound(&varg, cv, C_VARIANT_PAIR_CLOSE);
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
                        arg = va_arg(args, void *);
                        r = c_variant_read_one(cv, cv->levels + cv->i_levels, c, arg);
                        if (r < 0) {
                                c_variant_readv_default(&varg, c, args);
                                return r;
                        }
                        break;
                default:
                        return c_variant_poison(cv, -EMEDIUMTYPE);
                }
        }

        return 0;
}

/**
 * c_variant_rewind() - reset iterator
 * @cv:         variant to operate on, or NULL
 *
 * This resets the internal iterator position to the start of the variant (on
 * the root container level).
 *
 * It is an programming error to call this on an unsealed variant.
 */
_public_ void c_variant_rewind(CVariant *cv) {
        if (_unlikely_(!cv))
                return;

        assert(cv->sealed);

        while (c_variant_pop_level(cv))
                /* empty */ ;

        c_variant_level_root(cv->levels, cv, cv->levels->size);
}
