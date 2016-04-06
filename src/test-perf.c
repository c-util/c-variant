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
 * Performance Test
 * This contains basic serialization performance measurements, to compare plain
 * GVariant serialization to direct memory access. It is rather useless if run
 * as stand-alone benchmark. But is quite nice to get ballpark figures or add
 * custom extensions ad-hoc to compare features or get call-graphs on specific
 * subsystems.
 *
 * This tests transmits a simple structure of type "TestMessage" with a
 * trailing blob. It compares direct memory accesses of different kinds to a
 * serialization via CVariant. TestMessage is designed in a way to be binary
 * compatible to the GVariant marshaling format.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include "org.bus1/c-variant.h"

#define TEST_BUFSIZE (4096LL * 4096LL) /* 4096 pages */

typedef struct {
        uint32_t arg1;
        uint32_t arg2;
        uint64_t arg3;
        uint64_t size;
        uint8_t blob[];
} TestMessage;

static uint64_t nsec_from_clock(clockid_t clock) {
        struct timespec ts;
        int r;

        r = clock_gettime(clock, &ts);
        assert(r >= 0);
        return ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static int sys_memfd_create(const char *name, unsigned int flags) {
#ifndef __NR_memfd_create
        static_assert(false, "System lacks memfd_create(2) syscall");
#endif
        return syscall(__NR_memfd_create, name, flags);
}

static void test_message_write1(int fd, void *map, const TestMessage *args) {
        /*
         * Trivial transmitter that assumes the source representation is the
         * same as the wire representation, and memory can be accessed directly
         * mapped into the source address space. This is include to get a
         * baseline comparison for the other transmitters.
         */
        memcpy(map, args, sizeof(*args) + args->size);
}

static void test_message_write2(int fd, void *map, const TestMessage *args) {
        /*
         * This transmitter assumes the target is directly mapped into the
         * source address space. It then copies the source into the target,
         * according to a specific wire-format.
         *
         * This should be almost as fast as the direct transmitter, as the
         * marshaling overhead is trivial.
         */
        TestMessage *m = map;

        m->arg1 = args->arg1;
        m->arg2 = args->arg2;
        m->arg3 = args->arg3;
        m->size = args->size;
        memcpy(m->blob, args->blob, args->size);
}

static void test_message_write3(int fd, void *map, const TestMessage *args) {
        /*
         * This transmitter allocates a temporary stack object to marshal the
         * entire message. It then transmits the message via pwrite() into the
         * wire object.
         */
        TestMessage *m;
        uint64_t size;
        int r;

        size = sizeof(*m) + args->size;
        m = alloca(size);
        m->arg1 = args->arg1;
        m->arg2 = args->arg2;
        m->arg3 = args->arg3;
        m->size = args->size;
        memcpy(m->blob, args->blob, args->size);

        r = pwrite(fd, m, size, 0);
        assert(r >= 0 && (uint64_t)r == size);
}

static void test_message_write4(int fd, void *map, const TestMessage *args) {
        /*
         * This transmitter allocates a temporary stack object to marshal the
         * message header. It then transmits the message via pwritev() into the
         * wire object, taking one vector for the header and one for the blob,
         * thus avoiding a temporary copy of the blob.
         */
        struct iovec vec[2];
        TestMessage *m;
        int r;

        m = alloca(sizeof(*m));
        m->arg1 = args->arg1;
        m->arg2 = args->arg2;
        m->arg3 = args->arg3;
        m->size = args->size;

        vec[0].iov_base = (void *)m;
        vec[0].iov_len = sizeof(*m);
        vec[1].iov_base = (void *)args->blob;
        vec[1].iov_len = args->size;

        r = pwritev(fd, vec, 2, 0);
        assert(r >= 0 && (uint64_t)r == sizeof(*m) + args->size);
}

static void test_message_write5(int fd, void *map, const TestMessage *args) {
        /*
         * This transmitter avoids marshaling and rather uses an iovec for each
         * piece to transmit separately, thus making the kernel marshal the
         * message.
         */
        struct iovec vec[5];
        int r;

        vec[0].iov_base = (void *)&args->arg1;
        vec[0].iov_len = sizeof(args->arg1);
        vec[1].iov_base = (void *)&args->arg2;
        vec[1].iov_len = sizeof(args->arg2);
        vec[2].iov_base = (void *)&args->arg3;
        vec[2].iov_len = sizeof(args->arg3);
        vec[3].iov_base = (void *)&args->size;
        vec[3].iov_len = sizeof(args->size);
        vec[4].iov_base = (void *)args->blob;
        vec[4].iov_len = args->size;

        r = pwritev(fd, vec, 5, 0);
        assert(r >= 0 && (uint64_t)r == sizeof(TestMessage) + args->size);
}

static void test_message_write6(int fd, void *map, const TestMessage *args) {
        /*
         * This transmitter allocates a CVariant, marshals the message and
         * transmits it via pwritev() into the target descriptor.
         */
        const struct iovec *vecs;
        size_t n_vecs;
        CVariant *cv;
        int r;

        r = c_variant_new(&cv, "(uuttay)", 8);
        assert(r >= 0);

        c_variant_begin(cv, "(");
        c_variant_write(cv, "uutt",
                        args->arg1,
                        args->arg2,
                        args->arg3,
                        args->size);
        c_variant_insert(cv, "ay",
                         &(struct iovec){ .iov_base = (void *)args->blob, .iov_len = args->size },
                         1);
        c_variant_end(cv, ")");

        r = c_variant_seal(cv);
        assert(r >= 0);

        vecs = c_variant_get_vecs(cv, &n_vecs);

        r = pwritev(fd, vecs, n_vecs, 0);
        assert(r >= 0 && (uint64_t)r == sizeof(TestMessage) + args->size);

        c_variant_free(cv);
}

static void (* const test_xmitters[]) (int fd, void *map, const TestMessage *args) = {
        test_message_write1,
        test_message_write2,
        test_message_write3,
        test_message_write4,
        test_message_write5,
        test_message_write6,
};

static void test_message_validate(const void *map, const TestMessage *args) {
        const TestMessage *m = map;

        assert(args->arg1 == m->arg1);
        assert(args->arg2 == m->arg2);
        assert(args->arg3 == m->arg3);
        assert(args->size == m->size);
        assert(!memcmp(args->blob, m->blob, args->size));
}

static void test_message_xmit(int fd, void *map, const TestMessage *args, unsigned int xmitter) {
        assert(xmitter < sizeof(test_xmitters) / sizeof(*test_xmitters));

        test_xmitters[xmitter](fd, map, args);
        test_message_validate(map, args);
}

static void test_xmit(int fd, void *map, unsigned int xmitter, uint64_t times, uint64_t size) {
        static struct {
                TestMessage m;
                uint8_t blob[4096 * 4096];
        } m = {
                .m = {
                        .arg1 = UINT32_C(0xabcdabcd),
                        .arg2 = UINT32_C(0xffffffff),
                        .arg3 = UINT64_C(0xff00ff00ff00ff00),
                },
                .blob = {},
        };
        uint64_t i;

        assert(size <= sizeof(m.blob));

        m.m.size = size;
        for (i = 0; i < times; ++i)
                test_message_xmit(fd, map, &m.m, xmitter);
}

static void test_run_one(int fd, void *map, unsigned int xmitter, uint64_t times, uint64_t size) {
        uint64_t start_nsec, end_nsec;

        fprintf(stderr, "Run: times:%" PRIu64 " size:%" PRIu64 "\n", times, size);

        /* do some test runs to initialize caches; don't account them */
        memset(map, 0, TEST_BUFSIZE);
        test_xmit(fd, map, xmitter, times / 10, size);

        /* do real tests and measure time */
        start_nsec = nsec_from_clock(CLOCK_THREAD_CPUTIME_ID);
        test_xmit(fd, map, xmitter, times, size);
        end_nsec = nsec_from_clock(CLOCK_THREAD_CPUTIME_ID);

        /* print result table */
        printf("%" PRIu64 " %d %" PRIu64 "\n", size, xmitter, end_nsec - start_nsec);
}

static void test_run_all(int fd, void *map, unsigned int xmitter, uint64_t times) {
        unsigned int size;

        /*
         * Run test suite with different blob-sizes. First we start from size 1
         * to 128, doubling on each iteration. Then from 128 to 64k we just add
         * 128 each round to get better details.
         */

        for (size = 1; size <= 128; size <<= 1)
                test_run_one(fd, map, xmitter, times, size);
        for ( ; size <= 4096 << 4; size += 128)
                test_run_one(fd, map, xmitter, times, size);
}

static void test_transaction(unsigned int xmitter) {
        int memfd, r;
        uint8_t *map;
        long i;

        /* create memfd and pre-allocate buffer space */
        memfd = sys_memfd_create("test-file", MFD_CLOEXEC);
        assert(memfd >= 0);

        r = fallocate(memfd, 0, 0, TEST_BUFSIZE);
        assert(r >= 0);

        map = mmap(NULL, TEST_BUFSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
        assert(map != MAP_FAILED);

        /* access the whole buffer to do some random caching and fault in pages */
        for (i = 0; i < TEST_BUFSIZE; ++i)
                assert(map[i] == 0);

        /* run tests; each one 10k times */
        test_run_all(memfd, map, xmitter, 10UL * 1000UL);

        /* cleanup */
        munmap(map, TEST_BUFSIZE);
        close(memfd);
}

int main(int argc, char **argv) {
        unsigned int xmitter;

        if (argc != 2) {
                fprintf(stderr, "Usage: %s <#xmitter>\n", program_invocation_short_name);
                return 77;
        }

        xmitter = atoi(argv[1]);
        if (xmitter >= sizeof(test_xmitters) / sizeof(*test_xmitters)) {
                fprintf(stderr, "Invalid xmitter (available: %zu)\n",
                        sizeof(test_xmitters) / sizeof(*test_xmitters));
                return 77;
        }

        test_transaction(xmitter);
        return 0;
}
