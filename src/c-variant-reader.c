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
 * Word Handling
 * =============
 *
 * The specification defines a word as an:
 *     unaligned, little-endian integer big enough to represent the framing
 *     offset of any byte-position inside the parent container
 *
 * Words are used exclusively to store framing-offsets. Any real data is always
 * properly aligned and sized.
 */

static size_t c_variant_word_fetch(const void *addr, size_t wordsize) {
        /* read one word of wordsize '1 << wordsize' from @addr */
        switch (wordsize) {
        case 3: {
                uint64_t v;
                memcpy(&v, addr, 8);
                return le64toh(v);
        }
        case 2: {
                uint32_t v;
                memcpy(&v, addr, 4);
                return le32toh(v);
        }
        case 1: {
                uint16_t v;
                memcpy(&v, addr, 2);
                return le16toh(v);
        }
        case 0:
                return *(const uint8_t *)addr;
        default:
                assert(0);
                return 0;
        }
}

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

void c_variant_level_root(CVariantLevel *level, size_t size, const char *type, size_t n_type) {
        /*
         * Initialize the root-level to occupy @size bytes of the available
         * space. The caller should usually have calculated it based on the set
         * of iovecs available.
         */

        level->size = size;
        level->i_tail = size;
        level->v_tail = 0;
        level->wordsize = c_variant_word_size(size, 0);
        level->enclosing = C_VARIANT_TUPLE_OPEN;

        level->type = type;
        level->v_front = 0;
        level->i_front = 0;
        level->offset = 0;
        level->n_type = n_type;

        /*
         * For non-arrays, the index is the number of successfully parsed
         * dynamic size objects, plus 1. As we haven't parsed any dynamic sized
         * object, yet, set it to the initial 1.
         */
        level->index = 1;
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
                while (skip >= level->i_tail) {
                        assert(level->v_tail > 0);
                        level->i_tail += cv->vecs[--level->v_tail].iov_len;
                }

                /* fold tail, if @skip decreased compared to prev call */
                v = cv->vecs + level->v_tail;
                while (level->i_tail - skip > v->iov_len) {
                        assert(level->v_tail + 1 < cv->n_vecs);

                        level->i_tail -= v->iov_len;
                        ++level->v_tail;
                        ++v;
                }

                if (level->size < level->i_tail)
                        size = level->size - skip;
                else
                        size = level->i_tail - skip;

                p = (char *)v->iov_base + level->i_tail - skip - size;
        }

        *sizep = size;
        return p;
}

/*
 * Readers
 * =======
 *
 * XXX
 */

static int c_variant_peek(CVariant *cv,
                          char element,
                          CVariantType *infop,
                          size_t *sizep,
                          size_t *endp,
                          void **frontp) {
        CVariantLevel *level = cv->state->levels + cv->state->i_levels;
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

        if (_unlikely_(level->n_type < 1 || *level->type != element || level->index == 0))
                return c_variant_poison(cv, -EBADRQC);

        /* retrieve full type information and align front accordingly */
        r = c_variant_signature_next(level->type, level->n_type, infop);
        assert(r == 1);

        /* align front */
        offset = ALIGN_TO(level->offset, 1 << infop->alignment);
        level->i_front += offset - level->offset;
        level->offset = offset;

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
                        idx = (level->index - 1) * wz;
                        if (infop->n_type == level->n_type) {
                                if (idx <= level->size)
                                        offset = level->size - idx;
                        } else {
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
        case C_VARIANT_MAYBE:
        case C_VARIANT_ARRAY:
                --level->index;
                break;
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                if (info->size == 0)
                        ++level->index;
                /* fallthrough */
        default:
                level->type += info->n_type;
                level->n_type -= info->n_type;
                break;
        }
}

static int c_variant_enter_one(CVariant *cv, char container) {
        CVariantLevel *next, *level;
        CVariantType info;
        size_t size, end;
        int r;

        r = c_variant_ensure_level(cv);
        if (r < 0)
                return r;

        r = c_variant_peek(cv, container, &info, &size, &end, NULL);
        if (r < 0)
                return r;

        level = cv->state->levels + cv->state->i_levels;
        c_variant_push_level(cv);
        next = cv->state->levels + cv->state->i_levels;

        next->size = size;
        next->i_tail = level->i_front + size;
        next->v_tail = level->v_front;
        next->wordsize = c_variant_word_size(size, 0);
        next->enclosing = container;
        next->n_type = info.n_type - 1;
        next->v_front = level->v_front;
        next->i_front = level->i_front;
        next->index = 0;
        next->offset = 0;
        next->type = level->type + 1;

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
                                next->type = tail + tail_size - i;
                                next->n_type = i;
                                next->index = next->size - i;
                        }
                }
                if (next->index == 0) {
                        next->type = "()";
                        next->n_type = 2;
                        next->index = 1;
                }
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
                break;
        }

        c_variant_advance(cv, level, &info, end);
        return 0;
}

static void c_variant_exit_internal(CVariant *cv) {
        c_variant_pop_level(cv);
}

static int c_variant_exit_one(CVariant *cv) {
        if (c_variant_on_root_level(cv))
                return c_variant_poison(cv, -EBADRQC);

        c_variant_exit_internal(cv);
        return 0;
}

static int c_variant_exit_try(CVariant *cv, char container) {
        if (container != cv->state->levels[cv->state->i_levels].enclosing)
                return c_variant_poison(cv, -EBADRQC);

        return c_variant_exit_one(cv);
}

static int c_variant_read_one(CVariant *cv, char basic, void *arg) {
        static const char default_value[8] = {};
        CVariantType info;
        size_t size, end;
        void *front;
        int r;

        r = c_variant_peek(cv, basic, &info, &size, &end, &front);
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

        c_variant_advance(cv, cv->state->levels + cv->state->i_levels, &info, end);
        return 0;
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
        size_t i, size;
        char *p_type;
        int r;

        assert(type || n_type == 0);
        assert(vecs || n_vecs == 0);

        r = c_variant_signature_one(type, n_type, &info);
        if (r < 0)
                return r;

        r = c_variant_alloc(&cv, &p_type, NULL, n_type, info.n_levels + 8, n_vecs, 0);
        if (r < 0)
                return r;

        memcpy(p_type, type, n_type);
        memcpy(cv->vecs, vecs, n_vecs * sizeof(*vecs));
        cv->sealed = true;

        /*
         * You'd assume that if all iovecs are mapped in memory, an overflow in
         * 'size' could not happen. However, in case the mappings overlap, it
         * can. Hence, we must verify that the actual total size can be
         * represented in a single 'size_t'. We do not support reading variants
         * bigger than our native word. If you need this, do it yourself..
         */
        size = 0;
        for (i = 0; i < cv->n_vecs; ++i) {
                if (size + cv->vecs[i].iov_len < size) {
                        r = -EFBIG;
                        goto error;
                }
                size += cv->vecs[i].iov_len;
        }

        /*
         * So this is a bit questionable: If you create a new root level object
         * of a fixed-size type, you better use a buffer of the exact size.
         * Otherwise, you really do something stupid. So we should do this:
         *
         *     if (info.size > 0 && info.size != size)
         *             size = 0;
         *
         * However, imagine a deeply nested, complex, compound type of static
         * size. If you receive such a type from a remote party and want to
         * parse it, you actually *want* to make use of the defined
         * error-handling of GVariant, just as if it was a child of a
         * dynamic-sized object. Hence, we accept any size here.
         */
        c_variant_level_root(cv->state->levels + cv->state->i_levels, size, p_type, n_type);

        *cvp = cv;
        return 0;

error:
        c_variant_free(cv);
        return r;
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

        level = cv->state->levels + cv->state->i_levels;
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

        level = cv->state->levels + cv->state->i_levels;
        *sizep = level->n_type;
        return level->type;
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
                level = cv->state->levels + cv->state->i_levels;
                if (level->n_type < 1)
                        return c_variant_poison(cv, -EBADRQC);

                r = c_variant_enter_one(cv, *level->type);
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

static void c_variant_read_one_default(char basic, void *arg) {
        size_t size;

        static_assert(sizeof(double) == 8, "Unsupported 'double' type");

        if (!arg)
                return;

        size = 1;
        switch (basic) {
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
                memset(arg, 0, size);
                break;
        case C_VARIANT_STRING:
        case C_VARIANT_PATH:
        case C_VARIANT_SIGNATURE:
                *(const char **)arg = "";
                break;
        default:
                assert(0);
                break;
        }
}

static void c_variant_readv_default(CVariantVarg *varg, int c, va_list args) {
        const char *arg_s;
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
                switch (c) {
                case -1: /* level done, nothing to do */
                        break;
                case C_VARIANT_INT64:
                case C_VARIANT_UINT64:
                case C_VARIANT_DOUBLE:
                case C_VARIANT_INT32:
                case C_VARIANT_UINT32:
                case C_VARIANT_HANDLE:
                case C_VARIANT_INT16:
                case C_VARIANT_UINT16:
                case C_VARIANT_BOOL:
                case C_VARIANT_BYTE:
                case C_VARIANT_STRING:
                case C_VARIANT_PATH:
                case C_VARIANT_SIGNATURE:
                        arg = va_arg(args, void *);
                        c_variant_read_one_default(c, arg);
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
                        r = c_variant_read_one(cv, c, arg);
                        if (r < 0) {
                                c_variant_read_one_default(c, arg);
                                c_variant_readv_default(&varg, c_variant_varg_next(&varg), args);
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
        CVariantLevel *level;

        if (_unlikely_(!cv))
                return;

        assert(cv->sealed);

        while (!c_variant_on_root_level(cv))
                c_variant_exit_internal(cv);

        level = cv->state->levels + cv->state->i_levels;
        c_variant_level_root(level,
                             level->size,
                             level->type + level->n_type - cv->n_type,
                             cv->n_type);
}
