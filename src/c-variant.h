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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CVariant CVariant;

/**
 * Error Codes
 *
 * We usually return negative 'errno'-style error codes from all functions, in
 * case they can fail. All of them must be treated as programming errors or
 * fatal runtime exceptions. There is usually no reason why any of those errors
 * can occur at runtime, if suitable validation of input is done beforehand.
 * However, such validation is not always assumed necessary, hence, it is
 * perfectly valid to rely on the error codes.
 *
 * EBADRQC: Specified type does not match type of variant.
 * EFBIG: Scatter-gather array larger than the address space.
 * ELOOP: Nesting level of the GVariant type is higher than supported.
 * EMEDIUMTYPE: Invalid GVariant type/container specified.
 * EMSGSIZE: Message is larger than supported by this architecture. Very
 *           unlikely to happen, as you'd need type strings of large lengths.
 * ENOBUFS: Too many iovecs.
 * ENOMEM: Cannot allocate required backing memory.
 * ENOTUNIQ: Attempt to modify the NULL GVariant.
 */

/**
 * NULL GVariant
 *
 * The entire GVariant API accepts NULL as valid GVariant. Whenever it is
 * passed, it is treated as the unit type (type signature is "()"). However, we
 * cannot store any state in a NULL pointer, therefore, any attempt to modify
 * the internal state of the GVariant (including 'enter', 'exit', ...) will be
 * rejected with ENOTUNIQ. Any queries that do *not* alter the internal state
 * will be served as if you queried the unit type.
 */

/**
 * C_VARIANT_MAX_LEVEL - maximum container depth
 *
 * The GVariant specification is highly recursive and allows infinitive depths.
 * However, for security reasons, applications should limit the depth of types
 * they receive from untrusted sources. Otherwise, parsing complexity might
 * increase significantly. Hence, this implementation has a hard-coded, but
 * arbitrary, maximum nesting depth. The limit is chosen in a way that all
 * operations can be performed in reasonable times with any depth within the
 * supported range.
 * This limit defines the maximum nesting level (eg., '0' means only the root
 * level is supported, as such effectively only allowing basic types, '1' means
 * one container level is allowed, and so on..).
 *
 * Note that any type you send and/or receive should be hard-coded. You should
 * never build types dynamically. Hence, this limit should in no way affect the
 * runtime of your application (even if you deal with higher nesting depths
 * than this). You *must* verify types before parsing them.
 *
 * Note that this limit only applies to true type signatures. It does not apply
 * to nested variants (that is, real nesting via 'v' elements).
 */
#define C_VARIANT_MAX_LEVEL (255)

/**
 * C_VARIANT_MAX_SIGNATURE - maximum length of a signature
 *
 * GVariant types can be nested and concatenated arbitrarily. However, to
 * simplify the implementation, we enforce an arbitrary limit in the maximum
 * length of a type signature. As types are static, this limit should be
 * impossible to hit, but if you do, it's trivial to increase.
 *
 * Note that this limit only applies to true type signatures. It does not apply
 * to nested variants (that is, real nesting via 'v' elements).
 */
#define C_VARIANT_MAX_SIGNATURE (65535)

/**
 * C_VARIANT_MAX_VARG - maximum depth of vargs
 *
 * Several accessor functions use variable length argument lists to allow easy
 * access to read/write nested types. To process such requests, the
 * implementation must store state for each nesting level. To avoid runtime
 * overhead, there is a static limit on the maximum depth allowed in a single
 * function call. If you need higher depths, you are required to explicitly
 * enter/exit the containers.
 *
 * Exceeding this limit will result in a fatal exception, as it prevents us
 * from producing predictable behavior. Therefore, you better not pass deeply
 * nested types to dynamic accessor functions (it will become an unreadable
 * function call, anyway).
 */
#define C_VARIANT_MAX_VARG (16)

/* management */

int c_variant_new(CVariant **out, const char *type, size_t n_type, size_t hint_vecs, size_t hint_data);
int c_variant_new_from_vecs(CVariant **out, const char *type, size_t n_type, const struct iovec *vecs, size_t n_vecs);
CVariant *c_variant_free(CVariant *cv);

bool c_variant_is_sealed(CVariant *cv);
int c_variant_return_poison(CVariant *cv);

/* readers */

size_t c_variant_peek_count(CVariant *cv);
const char *c_variant_peek_type(CVariant *cv, size_t *sizep);

int c_variant_enter(CVariant *cv, const char *containers);
int c_variant_exit(CVariant *cv, const char *containers);
int c_variant_readv(CVariant *cv, const char *signature, va_list args);
void c_variant_rewind(CVariant *cv);

/* inline shortcuts */

/**
 * c_variant_new_from_buffer() - allocate new variant from linear data
 * @cvp:        output variable for new variant
 * @type:       type of this variant
 * @n_type:     length of @type in bytes
 * @data:       pointer to data buffer
 * @n_data:     length of @data
 *
 * This is a convenience wrapper around c_variant_new_from_vecs(), which just
 * accepts a single linear buffer, rather than a set of vectors.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int c_variant_new_from_buffer(CVariant **cvp, const char *type, size_t n_type, const void *data, size_t n_data) {
        struct iovec vec = { .iov_base = (void *)data, .iov_len = n_data };
        return c_variant_new_from_vecs(cvp, type, n_type, &vec, 1);
}

/**
 * c_variant_freep() - cleanup helper for c_variant_free()
 * @cv:         pointer to variant variable to clean up
 *
 * This is a helper for gcc cleanup-attributes. @cv has to point to the
 * variable pointing to your variant. If it is non-NULL, it is freed via
 * c_variant_free().
 */
static inline void c_variant_freep(CVariant **cv) {
        if (*cv)
                c_variant_free(*cv);
}

/**
 * c_variant_read() - read data from variant
 * @cv:         variant to read from
 * @signature:  signature string
 *
 * This is a convenience wrapper around c_variant_readv(), but accepting
 * arguments directly, rather than via va_list.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int c_variant_read(CVariant *cv, const char *signature, ...) {
        va_list args;
        int r;

        va_start(args, signature);
        r = c_variant_readv(cv, signature, args);
        va_end(args);
        return r;
}

#ifdef __cplusplus
}
#endif
