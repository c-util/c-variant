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
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
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

static void c_variant_word_store(void *addr, size_t wordsize, size_t value) {
        /* write one word of wordsize '1 << wordsize' to @addr */
        switch (wordsize) {
        case 3: {
                uint64_t v = htole64((uint64_t)value);
                memcpy(addr, &v, 8);
                return;
        }
        case 2: {
                uint32_t v = htole32((uint32_t)value);
                memcpy(addr, &v, 4);
                return;
        }
        case 1: {
                uint16_t v = htole16((uint16_t)value);
                memcpy(addr, &v, 2);
                return;
        }
        case 0:
                *(uint8_t *)addr = (uint8_t)value;
                return;
        default:
                assert(0);
                return;
        }
}

/*
 * Vectors
 * =======
 *
 * XXX
 */

static int c_variant_insert_vecs(CVariant *cv, size_t idx, size_t num) {
        struct iovec *v;
        size_t n;

        /*
         * This reallocates the iovec array and adds @num new vectors at
         * position @idx. All new vectors are reset to 0. We expect the caller
         * to be aware of front/end iterators and adjust them, in case the
         * reallocation moves them.
         *
         * Note that this might allocate more than @num vectors, as a reserve
         * for future requests. The caller must treat @num as a minimum.
         *
         * This also adjusts the trailing state-array, to actually reflect the
         * extended iovec array.
         */

        assert(idx <= cv->n_vecs);

        n = cv->n_vecs + num;
        if (_unlikely_(n < num || n > C_VARIANT_MAX_VECS))
                return c_variant_poison(cv, -ENOBUFS);

        /* allocate some more, to serve future requests */
        n = (n + 8 < C_VARIANT_MAX_VECS) ? n + 8 : C_VARIANT_MAX_VECS;
        num = n - cv->n_vecs;

        v = malloc(n * sizeof(*v) + n);
        if (!v)
                return c_variant_poison(cv, -ENOMEM);

        /* copy&extend trailing state-array */
        memcpy((char *)(v + n), (char *)(cv->vecs + cv->n_vecs), idx);
        memset((char *)(v + n) + idx, 0, num);
        memcpy((char *)(v + n) + idx + num, (char *)(cv->vecs + cv->n_vecs) + idx, (cv->n_vecs - idx));

        /* copy&extend actual iovec-array */
        memcpy(v, cv->vecs, idx * sizeof(*v));
        memset(v + idx, 0, num * sizeof(*v));
        memcpy(v + idx + num, cv->vecs + idx, (cv->n_vecs - idx) * sizeof(*v));

        if (cv->allocated_vecs)
                free(cv->vecs);
        else
                cv->allocated_vecs = true;

        cv->vecs = v;
        cv->n_vecs = n;
        return 0;
}

static void c_variant_swap_vecs(CVariant *cv, size_t a, size_t b) {
        struct iovec t;
        bool v;

        if (a != b) {
                /* swap vector data */
                t = cv->vecs[a];
                cv->vecs[a] = cv->vecs[b];
                cv->vecs[b] = t;

                /* swap trailing state byte */
                v = ((char *)(cv->vecs + cv->n_vecs))[a];
                ((char *)(cv->vecs + cv->n_vecs))[a] = ((char *)(cv->vecs + cv->n_vecs))[b];
                ((char *)(cv->vecs + cv->n_vecs))[b] = v;
        }
}

static int c_variant_reserve(CVariant *cv,
                             size_t n_extra_vecs,
                             size_t front_alignment,
                             size_t front_allocation,
                             void **frontp,
                             size_t tail_alignment,
                             size_t tail_allocation,
                             void **tailp) {
        CVariantLevel *level;
        size_t i, j, n, rem, n_front, n_tail;
        struct iovec *vec_front, *vec_tail;
        void *p;
        int r;

        /*
         * This advances the front and tail markers according to the requested
         * allocation size. If an alignment is given, the start is aligned
         * before the marker is advanced. If required, new buffer space is
         * allocated.
         *
         * On success, a pointer to the start of each reserved buffer space is
         * returned in @frontp and @tailp. On failure, both markers will stay
         * untouched.
         *
         * Note that front-alignment is always according to the global
         * alignment (i.e., it adheres to level->offset (and as such iov_base)
         * rather than level->i_front). But tail-alignment is always local-only
         * (adhering to level->i_tail). There is no global context for tail
         * space, so no way to align it as such.
         */

        /* both are mapped, hence cannot overflow size_t (with alignment) */
        assert(front_allocation + tail_allocation + 16 > front_allocation);

        level = cv->state->levels + cv->state->i_levels;
        n_front = front_allocation + ALIGN_TO(level->offset, 1 << front_alignment) - level->offset;
        n_tail = tail_allocation + ALIGN_TO(level->i_tail, 1 << tail_alignment) - level->i_tail;
        vec_front = cv->vecs + level->v_front;
        vec_tail = cv->vecs + cv->n_vecs - level->v_tail - 1;

        /*
         * If the remaining space is not enough to fullfill the request, search
         * through the unused vectors, in case there is unused buffer space
         * that is sufficient for the request. If we find one, move it directly
         * next to our current vector, so we can jump over.
         */
        if (n_front > vec_front->iov_len - level->i_front) {
                for (i = 1; vec_front + i < vec_tail; ++i) {
                        if (n_front > (vec_front + i)->iov_len)
                                continue;

                        c_variant_swap_vecs(cv,
                                            (vec_front + i) - cv->vecs,
                                            (vec_front + 1) - cv->vecs);
                        ++vec_front;
                        n_front = 0;
                        break;
                }
        } else if (n_front > 0) {
                /* fits into @vec_front */
                n_front = 0;
        }

        /* counter-part for tail-allocation */
        if (n_tail > vec_tail->iov_len - level->i_tail) {
                for (i = 1; vec_tail - i > vec_front; ++i) {
                        if (n_tail > (vec_tail - i)->iov_len)
                                continue;

                        c_variant_swap_vecs(cv,
                                            (vec_tail - i) - cv->vecs,
                                            (vec_tail - 1) - cv->vecs);
                        --vec_tail;
                        n_tail = 0;
                        break;
                }
        } else if (n_tail > 0) {
                /* fits into @vec_tail */
                n_tail = 0;
        }

        n = vec_tail - vec_front - 1;
        if (_unlikely_(n < n_extra_vecs + 2 * !!(n_front || n_tail))) {
                /* remember tail-index since realloc might move it */
                j = vec_front - cv->vecs;
                i = cv->n_vecs - (vec_tail - cv->vecs);

                r = c_variant_insert_vecs(cv, j + 1, n_extra_vecs + 2);
                if (r < 0)
                        return r;

                /* re-calculate vectors, as they might have moved */
                vec_front = cv->vecs + j;
                vec_tail = cv->vecs + cv->n_vecs - i;
        }

        /* if either is non-zero, we need a new buffer allocation */
        if (_unlikely_(n_front || n_tail)) {
                /*
                 * Now that we have the iovecs, we need the actual buffer
                 * space. We start with 2^12 bytes (4k / one page), and
                 * increase it for each allocated buffer by a factor of 2, up
                 * to an arbitrary limit of 2^31.
                 */
                n = 1 << (12 + ((cv->a_vecs > 19) ? 19 : cv->a_vecs));
                if (n < n_front + n_tail + 16)
                        n = n_front + n_tail + 16;

                p = malloc(n);
                if (!p) {
                        n = n_front + n_tail + 16;
                        p = malloc(n);
                        if (!p)
                                return c_variant_poison(cv, -ENOMEM);
                }

                /* count how often we allocated; protect against overflow */
                if (++cv->a_vecs < 1)
                        --cv->a_vecs;

                if (n_front) {
                        ++vec_front;
                        if (((char *)(cv->vecs + cv->n_vecs))[vec_front - cv->vecs])
                                free(vec_front->iov_base);

                        vec_front->iov_base = p;
                        vec_front->iov_len = n;
                        ((char *)(cv->vecs + cv->n_vecs))[vec_front - cv->vecs] = true;
                }

                if (n_tail) {
                        --vec_tail;
                        if (((char *)(cv->vecs + cv->n_vecs))[vec_tail - cv->vecs])
                                free(vec_tail->iov_base);

                        vec_tail->iov_base = p;
                        vec_tail->iov_len = n;
                        ((char *)(cv->vecs + cv->n_vecs))[vec_tail - cv->vecs] = true;
                }

                if (n_front && n_tail) {
                        /* if both allocated, we need to split properly */
                        rem = n - n_front - n_tail - 16;
                        vec_front->iov_len = n_front + 8 + (rem * C_VARIANT_FRONT_SHARE / 100);
                        vec_tail->iov_base = (char *)p + vec_front->iov_len;
                        vec_tail->iov_len = n - vec_front->iov_len;
                        ((char *)(cv->vecs + cv->n_vecs))[vec_tail - cv->vecs] = false;
                }
        }

        if (vec_front != cv->vecs + level->v_front) {
                /* vector was updated; clip previous and then advance */
                assert(vec_front - 1 == cv->vecs + level->v_front);

                (vec_front - 1)->iov_len = level->i_front;
                ++level->v_front;
                level->i_front = 0;

                /* front vectors must be aligned according to current offset */
                assert(vec_front->iov_base == ALIGN_PTR_TO(vec_front->iov_base, 8));
                n = level->offset & 7;
                vec_front->iov_base = (char *)vec_front->iov_base + n;
                vec_front->iov_len -= n;
        }

        if (vec_tail != cv->vecs + cv->n_vecs - level->v_tail - 1) {
                /* vector was updated; clip previous and then advance */
                assert(vec_tail + 1 == cv->vecs + cv->n_vecs - level->v_tail - 1);

                (vec_tail + 1)->iov_len = level->i_tail;
                ++level->v_tail;
                level->i_tail = 0;
        }

        /*
         * We are done! Apply alignment before returning a pointer to the
         * reserved space. Then advance the iterators, so the space is actually
         * reserved and will not get re-used.
         */

        n = ALIGN_TO(level->offset, 1 << front_alignment) - level->offset;
        memset((char *)vec_front->iov_base + level->i_front, 0, n);
        level->i_front += n;
        level->offset += n;
        level->i_tail = ALIGN_TO(level->i_tail, 1 << tail_alignment);

        if (frontp)
                *frontp = (char *)vec_front->iov_base + level->i_front;
        if (tailp)
                *tailp = (char *)vec_tail->iov_base + level->i_tail;

        level->i_front += front_allocation;
        level->offset += front_allocation;
        level->i_tail += tail_allocation;

        return 0;
}

/*
 * Writers
 * =======
 *
 * XXX
 */

static int c_variant_append(CVariant *cv,
                            char element,
                            CVariantType *info,
                            size_t n_extra_vecs,
                            size_t n_front,
                            void **frontp,
                            size_t n_unaccounted_tail,
                            void **tailp) {
        CVariantLevel *level = cv->state->levels + cv->state->i_levels;
        bool need_frame = false;
        void *tail;
        int r;

        /*
         * XXX
         */

        if (_unlikely_(level->n_type < 1 || *level->type != element))
                return c_variant_poison(cv, -EBADRQC);

        assert(info->size == 0 || n_front == 0 || n_front == info->size);

        switch (level->enclosing) {
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                if (info->n_type >= level->n_type)
                        break;
                /* fallthrough */
        case C_VARIANT_ARRAY:
                need_frame = (info->size < 1);
                break;
        }

        /*
         * If we need to store a frame pointer, we *must* guarantee 8-byte
         * alignment and allocate an extra 8 bytes at the tail.
         * We always additionally allocate @n_unaccounted_tail bytes at the
         * tail, which have *NO* alignment guarantees. But those bytes are
         * *unaccounted*, that is, we immediately subtract them from the tail
         * marker of this level again.
         */
        r = c_variant_reserve(cv, n_extra_vecs,
                              info->alignment, n_front, frontp,
                              need_frame ? 3 : 0,
                              n_unaccounted_tail + (need_frame ? 8 : 0),
                              &tail);
        if (r < 0)
                return r;

        /* de-account extra tail-space */
        assert(n_unaccounted_tail <= level->i_tail);
        level->i_tail -= n_unaccounted_tail;

        /* store frame */
        if (need_frame) {
                ++level->index;
                *(uint64_t *)tail = level->offset;
                tail = (char *)tail + 8;
        }

        switch (level->enclosing) {
        case C_VARIANT_ARRAY:
                break;
        case C_VARIANT_MAYBE:
                /* write maybe-marker for non-empty, dynamic maybes */
                if (info->size < 1)
                        ++level->index;
                /* fallthrough */
        default:
                level->type += info->n_type;
                level->n_type -= info->n_type;
                break;
        }

        if (tailp)
                *tailp = tail;
        return 0;
}

static int c_variant_begin_one(CVariant *cv, char container, const char *variant) {
        CVariantLevel *next, *level;
        CVariantType info;
        size_t n_tail;
        void *tail;
        int r;

        r = c_variant_ensure_level(cv);
        if (r < 0)
                return r;

        if (container == C_VARIANT_VARIANT)
                n_tail = strlen(variant);
        else
                n_tail = 0;

        level = cv->state->levels + cv->state->i_levels;
        if (_unlikely_(level->n_type < 1))
                return c_variant_poison(cv, -EBADRQC);

        r = c_variant_signature_next(level->type, level->n_type, &info);
        assert(r == 1);

        r = c_variant_append(cv, container, &info, 0, 0, NULL, n_tail, &tail);
        if (r < 0)
                return r;

        c_variant_push_level(cv);
        next = cv->state->levels + cv->state->i_levels;

        next->size = info.size;
        next->i_tail = level->i_tail;
        next->v_tail = level->v_tail;
        /* wordsize is unused */
        next->enclosing = container;
        next->v_front = level->v_front;
        next->i_front = level->i_front;
        next->index = 0;
        next->offset = 0;

        switch (container) {
        case C_VARIANT_VARIANT:
                memcpy(tail, variant, n_tail);
                next->i_tail += n_tail;
                next->n_type = n_tail;
                next->index = n_tail;
                next->type = tail;
                break;
        case C_VARIANT_MAYBE:
        case C_VARIANT_ARRAY:
                next->n_type = info.n_type - 1;
                next->type = info.type + 1;
                break;
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                next->n_type = info.n_type - 2;
                next->type = info.type + 1;
                break;
        default:
                assert(0);
                break;
        }

        return 0;
}

static int c_variant_end_one(CVariant *cv) {
        CVariantLevel *prev, *level;
        size_t i, n, wz, rem;
        void *front, *tail;
        struct iovec *v;
        uint64_t frame;
        int r, step;

        if (_unlikely_(c_variant_on_root_level(cv)))
                return c_variant_poison(cv, -EBADRQC);

        prev = cv->state->levels + cv->state->i_levels;
        wz = c_variant_word_size(prev->offset, prev->index);

        switch (prev->enclosing) {
        case C_VARIANT_VARIANT:
                n = prev->index + 1;
                break;
        case C_VARIANT_MAYBE:
                n = !!(prev->index > 0);
                break;
        case C_VARIANT_ARRAY:
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                n = prev->index * (1 << wz);
                break;
        default:
                assert(0);
                return c_variant_poison(cv, -EFAULT);
        }

        if (prev->size < 1) {
                /*
                 * Variable-size container which requires 'n' additional front
                 * bytes for framing-offsets and other management data. No
                 * alignment is enforced, not is trailing padding added.
                 */
                r = c_variant_reserve(cv, 0, 0, n, &front, 0, 0, &tail);
                if (r < 0)
                        return r;
        } else {
                /*
                 * Fixed-size container of size prev->size. We *must* ensure
                 * the container has a size multiple of its alignment, hence,
                 * we add trailing zero bytes as padding here.
                 */
                assert(!n);
                assert(prev->offset <= prev->size);

                n = prev->size - prev->offset;
                r = c_variant_reserve(cv, 0, 0, n, &front, 0, 0, &tail);
                if (r < 0)
                        return r;

                memset(front, 0, n);
        }

        c_variant_pop_level(cv);
        level = cv->state->levels + cv->state->i_levels;

        switch (prev->enclosing) {
        case C_VARIANT_VARIANT:
                *(char *)front = 0;
                memcpy((char *)front + 1, (char *)tail - prev->index, prev->index);
                break;
        case C_VARIANT_MAYBE:
                if (prev->index > 0)
                        *(char *)front = 0;
                break;
        case C_VARIANT_ARRAY:
        case C_VARIANT_TUPLE_OPEN:
        case C_VARIANT_PAIR_OPEN:
                /* backwards-iteration for arrays, to revert frame oder */
                if (prev->enclosing == C_VARIANT_ARRAY) {
                        i = prev->index - 1;
                        step = -1;
                } else {
                        i = 0;
                        step = 1;
                }

                v = cv->vecs + cv->n_vecs - prev->v_tail - 1;
                rem = prev->i_tail;

                for (n = prev->index; n-- > 0; i += step) {
                        while (_unlikely_(rem < 8)) {
                                assert(rem == 0);
                                ++v;
                                rem = v->iov_len;
                                assert(!(rem & 7));
                        }

                        rem -= 8;
                        c_variant_word_store((char *)front + i * (1 << wz), wz,
                                             *(uint64_t *)((char *)v->iov_base + rem));
                }

                break;
        }

        /*
         * Advance parent level by the size of the completed child. Note that
         * the parent-level was already aligned correctly when entered. Hence,
         * prev->offset correctly reflects the difference in bytes between
         * both fronts.
         */
        level->v_front = prev->v_front;
        level->i_front = prev->i_front;
        level->offset += prev->offset;

        /*
         * If this was a dynamic-sized type, we must store the framing-offset
         * at the tail. Memory for it was already reserved when the container
         * was created, we just recover the pointer to it and write the now
         * known framing offset.
         * Note that for tuples we never write a framing offset for the last
         * type. This also guarantees that the root-level never writes framing
         * offsets (root-level can only be a single type, rather than a full
         * signature).
         * Containers with a single entry never store framing offsets. In those
         * cases we can skip the operation.
         *
         * This only stores out internal state-tracking at the tail buffer.
         * This is *not* the final serialized data. Only once the full
         * container is closed, the state-array is properly serialized.
         */
        if (prev->size < 1) {
                switch (level->enclosing) {
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        /* last element never stores framing offsets */
                        if (level->n_type < 1)
                                break;
                        /* fallthrough */
                case C_VARIANT_ARRAY:
                        assert(level->i_tail >= 8);
                        assert(!(level->i_tail & 7));

                        v = cv->vecs + cv->n_vecs - level->v_tail - 1;
                        frame = level->offset;
                        memcpy((char *)v->iov_base + level->i_tail - 8, &frame, 8);
                        break;
                }
        }

        return 0;
}

static int c_variant_end_try(CVariant *cv, char container) {
        if (container != cv->state->levels[cv->state->i_levels].enclosing)
                return c_variant_poison(cv, -EBADRQC);

        return c_variant_end_one(cv);
}

static int c_variant_write_one(CVariant *cv, char basic, const void *arg, size_t n_arg) {
        CVariantLevel *level;
        CVariantType info;
        void *front;
        int r;

        assert(n_arg > 0);

        level = cv->state->levels + cv->state->i_levels;
        if (_unlikely_(level->n_type < 1))
                return c_variant_poison(cv, -EBADRQC);

        r = c_variant_signature_next(level->type, level->n_type, &info);
        assert(r == 1);

        r = c_variant_append(cv, basic, &info, 0, n_arg, &front, 0, NULL);
        if (r < 0)
                return r;

        memcpy(front, arg, n_arg);
        return 0;
}

static int c_variant_insert_one(CVariant *cv, const char *type, const struct iovec *vecs, size_t n_vecs, size_t size) {
        CVariantLevel *level;
        CVariantType info;
        size_t n_type, i, idx;
        struct iovec *v;
        uint64_t frame;
        int r;

        level = cv->state->levels + cv->state->i_levels;
        if (_unlikely_(level->n_type < 1))
                return c_variant_poison(cv, -EBADRQC);

        r = c_variant_signature_next(level->type, level->n_type, &info);
        assert(r == 1);

        n_type = strlen(type);
        if (_unlikely_(n_type != info.n_type || strncmp(type, info.type, n_type)))
                return c_variant_poison(cv, -EBADRQC);

        if (_unlikely_(info.size > 0 && size != info.size))
                return c_variant_poison(cv, -EBADMSG);

        r = c_variant_append(cv, *type, &info, n_vecs + 1, 0, NULL, 0, NULL);
        if (r < 0)
                return r;

        /* make sure there are at least 'n_vecs + 1' unused vectors */
        assert(cv->n_vecs - level->v_front - level->v_tail - 2U >= n_vecs + 1U);

        /*
         * Clip the current front and prepare the next vector with the
         * remaining buffer space. Then insert the requested vectors in between
         * both and verify alignment restrictions.
         */
        v = cv->vecs + level->v_front;
        v[n_vecs + 1].iov_base = (char *)v->iov_base + level->i_front;
        v[n_vecs + 1].iov_len = v->iov_len - level->i_front;
        v->iov_len = level->i_front;

        for (i = 0; i < n_vecs; ++i) {
                idx = level->v_front + i + 1;
                if (((char *)(cv->vecs + cv->n_vecs))[idx]) {
                        ((char *)(cv->vecs + cv->n_vecs))[idx] = false;
                        free((cv->vecs + idx)->iov_base);
                }
                cv->vecs[idx] = vecs[i];
        }

        level->v_front += n_vecs + 1;
        level->i_front = 0;
        level->offset += size;

        /* see c_variant_end_one(); we have to update the framing offset */
        if (info.size < 1) {
                switch (level->enclosing) {
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        /* last element never stores framing offsets */
                        if (level->n_type < 1)
                                break;
                        /* fallthrough */
                case C_VARIANT_ARRAY:
                        assert(level->i_tail >= 8);
                        assert(!(level->i_tail & 7));

                        v = cv->vecs + cv->n_vecs - level->v_tail - 1;
                        frame = level->offset;
                        memcpy((char *)v->iov_base + level->i_tail - 8, &frame, 8);
                        break;
                }
        }

        return 0;
}

/**
 * c_variant_new() - create new variant ready for writing
 * @cvp:        output variable for new variant
 * @type:       type string
 * @n_type:     length of @type
 *
 * This allocates a new variant of type @type. It is unsealed and ready for
 * serialization. Any data that will be allocated during serialization, will be
 * dynamically allocated and is owned by the variant itself.
 *
 * The type string is copied into the variant and can be released by the
 * caller.
 *
 * On success, the new variant is returned in @cvp. On failure, @cvp stays
 * untouched.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_new(CVariant **cvp, const char *type, size_t n_type) {
        CVariantLevel *level;
        CVariantType info;
        CVariant *cv;
        size_t size;
        char *p_type;
        void *extra;
        int r;

        assert(type || n_type == 0);

        r = c_variant_signature_one(type, n_type, &info);
        if (r < 0)
                return r;

        /* allocate 2k as initial buffer, except if fixed size */
        size = info.size ?: ALIGN_TO(2048, 8);

        r = c_variant_alloc(&cv, &p_type, &extra, n_type, info.n_levels + 8, 4, size);
        if (r < 0)
                return r;

        memcpy(p_type, type, n_type);
        memset(cv->vecs, 0, cv->n_vecs * sizeof(*cv->vecs));

        /* split memory between front and tail */
        cv->vecs[0].iov_base = extra;
        cv->vecs[0].iov_len = ALIGN_TO(size * C_VARIANT_FRONT_SHARE / 100, 8);
        cv->vecs[cv->n_vecs - 1].iov_base = (char *)extra + cv->vecs[0].iov_len;
        cv->vecs[cv->n_vecs - 1].iov_len = size - cv->vecs[0].iov_len;

        level = cv->state->levels + cv->state->i_levels;
        level->size = info.size;
        level->i_tail = 0;
        level->v_tail = 0;
        level->wordsize = 0;
        level->enclosing = C_VARIANT_TUPLE_OPEN;
        level->n_type = n_type;
        level->v_front = 0;
        level->i_front = 0;
        level->index = 0;
        level->offset = 0;
        level->type = p_type;

        *cvp = cv;
        return 0;
}

/**
 * c_variant_beginv() - begin a new container
 * @cv:         variant to operate on, or NULL
 * @containers: containers to write, or NULL
 * @args:       additional parameters
 *
 * This begins writing a new container to @cv, moving the iterator into the
 * container for following writes. The containers to enter have to be specified
 * via @containers (if NULL, the next container is entered). Whenever you enter
 * a variant, you must specify the type for the entire variant as another
 * argument in @args.
 *
 * Valid elements for @containers are:
 *   'v' to begin a variant
 *   'm' to begin a maybe
 *   'a' to begin an array
 *   '(' to begin a tuple
 *   '{' to begin a pair
 *
 * It is an programming error to call this on a sealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_beginv(CVariant *cv, const char *containers, va_list args) {
        CVariantLevel *level;
        const char *type;
        int r;

        if (_unlikely_(!cv))
                return -ENOTUNIQ;

        assert(!cv->sealed);

        if (containers) {
                for ( ; *containers; ++containers) {
                        type = NULL;
                        switch (*containers) {
                        case C_VARIANT_VARIANT:
                                type = va_arg(args, const char *);
                                /* fallthrough */
                        case C_VARIANT_MAYBE:
                        case C_VARIANT_ARRAY:
                        case C_VARIANT_TUPLE_OPEN:
                        case C_VARIANT_PAIR_OPEN:
                                r = c_variant_begin_one(cv, *containers, type);
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

                if (*level->type == C_VARIANT_VARIANT)
                        type = va_arg(args, const char *);
                else
                        type = NULL;

                r = c_variant_begin_one(cv, *level->type, type);
                if (r < 0)
                        return r;
        }

        return 0;
}

/**
 * c_variant_end() - end a container
 * @cv:         variant to operate on, or NULL
 * @containers: containers to write, or NULL
 *
 * This function is the counter-part to c_variant_beginv() (see its
 * documentation for details). It works very similar, but rather than starting
 * a new container, it finishes one and returns to the parent container.
 *
 * Valid elements for @containers are:
 *   'v' to end a variant
 *   'm' to end a maybe
 *   'a' to end an array
 *   ')' to end a tuple
 *   '}' to end a pair
 *
 * It is an programming error to call this on a sealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_end(CVariant *cv, const char *containers) {
        char enclosing;
        int r;

        if (_unlikely_(!cv))
                return -ENOTUNIQ;

        assert(!cv->sealed);

        if (!containers)
                return c_variant_end_one(cv);

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

                r = c_variant_end_try(cv, enclosing);
                if (r < 0)
                        return r;
        }

        return 0;
}

/**
 * c_variant_writev() - write data
 * @cv:         variant to operate on, or NULL
 * @signature:  signature string
 * @args:       additional parameters
 *
 * This advances the internal iterator of @cv according to the signature string
 * @signature, serializing each type and storing the information to the
 * variant.
 *
 * This function reads one type after another from the signature, until it hits
 * the end of the signature. For each type, the following operation is done:
 *   - basic types: The caller must provide the data to store in the variant.
 *                  For any fixed-size type equal to, or smaller than, 4 bytes,
 *                  the data must be provided directly as an "int" or "unsigned
 *                  int" as argument. Any 8 byte type must be provided as 8
 *                  byte variable directly as argument.
 *                  Any dynamic sized type (e.g., strings) must be provided as
 *                  pointer (must not be NULL).
 *   - variants: For every variant you specify, you must provide the type
 *               string in @args (must not be NULL). The variant is then
 *               entered and recursively written to.
 *   - maybe: For every maybe you specify, you must provide a boolean in @args
 *            which specifies whether the maybe should be entered, or whether
 *            it should be written empty.
 *   - array: For every array, you must provide an integer in @args, specifying
 *            the number of elements to write. All those are then recursively
 *            written into the variant.
 *   - tuple/pair: Tuples and pairs are simply entered/exited as specified.
 *
 * Type processing stops at the first error, which will then be returned to the
 * caller.
 *
 * It is an programming error to call this on a sealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_writev(CVariant *cv, const char *signature, va_list args) {
        CVariantVarg varg;
        const char *arg_s;
        uint64_t arg_64;
        uint32_t arg_32;
        uint16_t arg_16;
        uint8_t arg_8;
        int arg_i;
        int r, c;

        if (_unlikely_(!signature || !*signature))
                return 0;

        if (_unlikely_(!cv))
                return strcmp(signature, "()") ? -EBADRQC : 0;

        assert(!cv->sealed);

        for (c = c_variant_varg_init(&varg, signature, strlen(signature));
             c;
             c = c_variant_varg_next(&varg)) {
                switch (c) {
                case -1: /* level done */
                        c_variant_end_one(cv);
                        break;
                case C_VARIANT_VARIANT:
                        arg_s = va_arg(args, const char *);
                        r = c_variant_begin_one(cv, c, arg_s);
                        if (r < 0)
                                return r;

                        c_variant_varg_push(&varg, arg_s, strlen(arg_s), -1);
                        break;
                case C_VARIANT_MAYBE:
                case C_VARIANT_ARRAY:
                        r = c_variant_begin_one(cv, c, NULL);
                        if (r < 0)
                                return r;

                        /* bool in varargs becomes int */
                        arg_i = va_arg(args, int);
                        c_variant_varg_enter_bound(&varg, cv, arg_i);
                        break;
                case C_VARIANT_TUPLE_OPEN:
                case C_VARIANT_PAIR_OPEN:
                        r = c_variant_begin_one(cv, c, NULL);
                        if (r < 0)
                                return r;

                        if (c == C_VARIANT_TUPLE_OPEN)
                                c_variant_varg_enter_unbound(&varg, cv, C_VARIANT_TUPLE_CLOSE);
                        else
                                c_variant_varg_enter_unbound(&varg, cv, C_VARIANT_PAIR_CLOSE);
                        break;
                case C_VARIANT_INT64:
                case C_VARIANT_UINT64:
                case C_VARIANT_DOUBLE:
                        arg_64 = va_arg(args, uint64_t);
                        r = c_variant_write_one(cv, c, &arg_64, sizeof(arg_64));
                        if (r < 0)
                                return r;
                        break;
                case C_VARIANT_INT32:
                case C_VARIANT_UINT32:
                case C_VARIANT_HANDLE:
                        arg_32 = va_arg(args, uint32_t);
                        r = c_variant_write_one(cv, c, &arg_32, sizeof(arg_32));
                        if (r < 0)
                                return r;
                        break;
                case C_VARIANT_INT16:
                case C_VARIANT_UINT16:
                        arg_16 = va_arg(args, unsigned int);
                        r = c_variant_write_one(cv, c, &arg_16, sizeof(arg_16));
                        if (r < 0)
                                return r;
                        break;
                case C_VARIANT_BOOL:
                case C_VARIANT_BYTE:
                        arg_8 = va_arg(args, unsigned int);
                        r = c_variant_write_one(cv, c, &arg_8, sizeof(arg_8));
                        if (r < 0)
                                return r;
                        break;
                case C_VARIANT_STRING:
                case C_VARIANT_PATH:
                case C_VARIANT_SIGNATURE:
                        arg_s = va_arg(args, const char *);
                        r = c_variant_write_one(cv, c, arg_s, strlen(arg_s) + 1);
                        if (r < 0)
                                return r;
                        break;
                default:
                        return c_variant_poison(cv, -EMEDIUMTYPE);
                }
        }

        return 0;
}

/**
 * c_variant_insert() - insert iovecs
 * @cv:         variant to operate on, or NULL
 * @type:       type string
 * @vecs:       iovec array
 * @n_vecs:     number of vectors in @vecs
 *
 * This works similar to c_variant_writev(), but rather than copying data into
 * the variant, it inserts the passed iovecs directly, without deep-copying the
 * actual payload. This assumes the caller properly serialized the data
 * according to the GVariant specification. No verification is done. Note that
 * only a single type can be inserted, rather than a stream of types, since the
 * caller cannot deduce the constraints without also knowing the surrounding
 * type. This would make the API non-straightforward to use, so we expect
 * callers to properly separate types.
 *
 * If the data to be inserted is incorrectly marshaled, none of the surrounding
 * data is affected. That is, a proper receiver of the message will be able to
 * decode anything properly, and gets the verbatim data exactly for the block
 * inserted with this call.
 *
 * It is an programming error to call this on a sealed variant.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_insert(CVariant *cv, const char *type, const struct iovec *vecs, size_t n_vecs) {
        size_t i, size;

        size = 0;
        for (i = 0; i < n_vecs; ++i) {
                if (size + vecs[i].iov_len < size)
                        return cv ? c_variant_poison(cv, -EFBIG) : -EFBIG;
                size += vecs[i].iov_len;
        }

        if (_unlikely_(!cv)) {
                if (strcmp(type, "()"))
                        return -EBADRQC;
                if (size != 1)
                        return -ENOTUNIQ;
                return 0;
        }

        return c_variant_insert_one(cv, type, vecs, n_vecs, size);
}

/**
 * c_variant_seal() - seal a container
 * @cv:         variant to operate on, or NULL
 *
 * This rewinds a variant and seals it. Afterwards, no more modifications are
 * allowed on the variant.
 *
 * If the variant is already sealed, this just rewinds it.
 *
 * Return: 0 on success, negative error code on failure.
 */
_public_ int c_variant_seal(CVariant *cv) {
        CVariantLevel *level;
        size_t i;
        int r;

        if (_unlikely_(!cv))
                return 0;
        if (_unlikely_(cv->sealed)) {
                c_variant_rewind(cv);
                return 0;
        }

        /* close all open containers */
        while (!c_variant_on_root_level(cv)) {
                r = c_variant_end_one(cv);
                if (r < 0)
                        return r;
        }

        level = cv->state->levels + cv->state->i_levels;

        /* clip trailing vector */
        cv->vecs[level->v_front].iov_len = level->i_front;

        /* release all unused vectors */
        for (i = level->v_front + 1; i < cv->n_vecs; ++i)
                if (((char *)(cv->vecs + cv->n_vecs))[i])
                        free((void *)((unsigned long)cv->vecs[i].iov_base & ~7));

        /* move trailing state array up-front and shrink iovec array */
        memmove(&cv->vecs[level->v_front + 1], &cv->vecs[cv->n_vecs], level->v_front + 1);
        cv->n_vecs = level->v_front + 1;

        cv->sealed = true;
        c_variant_level_root(level,
                             level->offset,
                             level->type + level->n_type - cv->n_type,
                             cv->n_type);
        return 0;
}
