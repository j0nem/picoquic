/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <string.h>
#ifdef _WINDOWS
#include <malloc.h>
#endif

#include "picoquic_internal.h"
#include "picoquic_utils.h"
#include "picohash.h"

struct hashtestkey {
    uint64_t x;
    picohash_item item;
};

static uint64_t hashtest_hash(const void* v, const uint8_t* hash_seed)
{
    const struct hashtestkey* k = (const struct hashtestkey*)v;
    uint64_t hash = (k->x + 0xDEADBEEFull);
    return hash;
}

static int hashtest_compare(const void* v1, const void* v2)
{
    const struct hashtestkey* k1 = (const struct hashtestkey*)v1;
    const struct hashtestkey* k2 = (const struct hashtestkey*)v2;

    return (k1->x == k2->x) ? 0 : -1;
}

static picohash_item * hashtest_key_to_item(const void * key)
{
    struct hashtestkey* p = (struct hashtestkey*)key;

    return &p->item;
}

static struct hashtestkey* hashtest_item(uint64_t x)
{
    struct hashtestkey* p = (struct hashtestkey*)malloc(sizeof(struct hashtestkey));

    if (p != NULL) {
        memset(p, 0, sizeof(struct hashtestkey));
        p->x = x;
    }

    return p;
}

int picohash_test_one(int embedded_item)
{
    /* Create a hash table */
    int ret = 0;
    picohash_table* t = NULL;
    uint8_t hash_seed[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    
    if (!embedded_item) {
        t = picohash_create(32, hashtest_hash, hashtest_compare);
    }
    else {
        t = picohash_create_ex(32, hashtest_hash, hashtest_compare, hashtest_key_to_item, hash_seed);
    }

    if (t == NULL) {
        DBG_PRINTF("%s", "picohash_create() failed\n");
        ret = -1;
    } else {
        struct hashtestkey hk;

        if (t->count != 0) {
            DBG_PRINTF("picohash empty table count != 0 (count=%"PRIst")\n", t->count);
            ret = -1;
        }

        /* Enter a bunch of values, all different */
        for (uint64_t i = 1; ret == 0 && i < 10; i += 2) {
            if (picohash_insert(t, hashtest_item(i)) != 0) {
                DBG_PRINTF("picohash_insert(%"PRId64") failed\n", i);
                ret = -1;
            }
        }

        if (t->count != 5) {
            DBG_PRINTF("picohash table count != 5 (count=%"PRIst")\n", t->count);
            ret = -1;
        }

        /* Test whether each value can be retrieved */
        for (uint64_t i = 1; ret == 0 && i < 10; i += 2) {
            hk.x = i;
            picohash_item* pi = picohash_retrieve(t, &hk);

            if (pi == NULL) {
                DBG_PRINTF("picohash_retrieve(%"PRId64") failed\n", i);
                ret = -1;
            }
        }

        /* Create a bunch of collisions */
        for (uint64_t k = 1; ret == 0 && k < 6; k += 4) {
            for (uint64_t j = 1; ret == 0 && j <= k; j++) {
                if (picohash_insert(t, hashtest_item(k + 32 * j)) != 0) {
                    DBG_PRINTF("picohash_insert(%"PRId64" + 32 * %"PRId64") failed\n", k, j);
                    ret = -1;
                }
            }
        }

        if (t->count != 11) {
            DBG_PRINTF("picohash table count != 11 (count=%"PRIst")\n", t->count);
            ret = -1;
        }

        /* Check that the collisions can be retrieved */
        for (uint64_t k = 1; ret == 0 && k < 6; k += 4) {
            for (uint64_t j = 1; ret == 0 && j <= k; j++) {
                hk.x = k + 32 * j;
                picohash_item* pi = picohash_retrieve(t, &hk);

                if (pi == NULL) {
                    DBG_PRINTF("picohash_retrieve(%"PRId64" + 32 * %"PRId64") failed\n", k, j);
                    ret = -1;
                }
            }
        }

        /* Test whether different values cannot be retrieved */
        for (uint64_t i = 0; ret == 0 && i <= 10; i += 2) {
            hk.x = i;
            picohash_item* pi = picohash_retrieve(t, &hk);

            if (pi != NULL) {
                DBG_PRINTF("picohash_retrieve(%"PRId64") returned invalid item\n", i);
                ret = -1;
            }
        }

        /* Delete first, last and middle */
        for (uint64_t i = 1; ret == 0 && i < 10; i += 4) {
            hk.x = i;
            picohash_item* pi = picohash_retrieve(t, &hk);

            if (pi == NULL) {
                DBG_PRINTF("picohash_retrieve(%"PRId64") failed\n", i);
                ret = -1;
            } else {
                picohash_delete_item(t, pi, 1);
            }
        }

        /* Check that the deleted are gone */

        if (t->count != 8) {
            DBG_PRINTF("picohash table count != 8 (count=%"PRIst")\n", t->count);
            ret = -1;
        }

        for (uint64_t i = 1; ret == 0 && i < 10; i += 4) {
            hk.x = i;
            picohash_item* pi = picohash_retrieve(t, &hk);

            if (pi != NULL) {
                DBG_PRINTF("picohash_retrieve(%"PRId64") deleted value still found\n", i);
                ret = -1;
            }
        }

        /* Delete the table */
        picohash_delete(t, 1);
    }

    return ret;
}

int picohash_test()
{
    return(picohash_test_one(0));
}

int picohash_embedded_test()
{
    return(picohash_test_one(1));
}

/* Test the behavior of the basic hash
 */
void hash_test_init(uint8_t* test, size_t length, uint8_t * k, size_t k_length)
{
    /* Create a test string */
    for (size_t i = 0; i < length; i++) {
        test[i] = (uint8_t)(i + (i >> 8));
    }

    for (size_t i = 0; i < k_length; i++)
    {
        k[i] = (uint8_t)(k_length - i);
    }
}

int picohash_bytes_test()
{
    uint8_t test[1024];
    uint8_t k[16];
    int ret = 0;
    size_t test_lengths[12] = { 1, 3, 7, 8, 12, 16, 17, 31, 127, 257, 515, 1024 };
    uint64_t href[12] = {
        0x37123de71961bf68,
        0x5d162573aac6c6dd,
        0xf4bf76a59f4b1f6c,
        0xba2ddad4a019fb75,
        0xb853aa3d392912dc,
        0xdfced79f97377c86,
        0x9519a480fe7d7355,
        0x45c2f124bbb2945c,
        0x52360a8f840ff895,
        0x91010a2ff18acce8,
        0x67870e8e28f784bf,
        0x8bf35ec0fe7139a6
    };

    hash_test_init(test, sizeof(test), k, sizeof(k));

    /* Compute or check the reference siphash value */
    for (size_t i = 0; i < sizeof(test_lengths) / sizeof(size_t); i++) {
        uint64_t h = picohash_bytes(test, (uint32_t)test_lengths[i], k);
        if (h != href[i]) {
            DBG_PRINTF("H[%zu] = %" PRIx64 " instead of %"PRIx64, i, h, href[i]);
#if COMPUTING_REFERENCE_BASIC_VALUE
            href[i] = h;
#else
            ret = -1;
            break;
#endif
        }
    }
    return ret;
}

/* Test of the siphash function
 */
int siphash(const void* in, const size_t inlen, const void* k, uint8_t* out,
    const size_t outlen);

static uint64_t low_endian_64(uint8_t* b)
{
    uint64_t x =
        (uint64_t)b[0] + \
        (((uint64_t)b[1]) << 8) + \
        (((uint64_t)b[2]) << 16) + \
        (((uint64_t)b[3]) << 24) + \
        (((uint64_t)b[4]) << 32) + \
        (((uint64_t)b[5]) << 40) + \
        (((uint64_t)b[6]) << 48) + \
        (((uint64_t)b[7]) << 56);
    return x;
}

int siphash_test()
{
    uint8_t test[1024];
    uint8_t sip_out[8];
    uint8_t k[16];
    size_t test_lengths[12] = { 1, 3, 7, 8, 12, 16, 17, 31, 127, 257, 515, 1024 };
    uint64_t href[12] = {
        0xa9b786935f98d6b8,
        0x3fb64f2d81ebf107,
        0xcd34491a7b437e1b,
        0x5fbe917709286bc4,
        0xb2cc76e0f81d6e2f,
        0x09e69c0f70753651,
        0xc615b5349acc0cc2,
        0x965379fb0e26e150,
        0x85a286cfc4a62574,
        0x5f774367aeea9f83,
        0xd04ee1d420e9bc22,
        0x0a7ad6655680779e
    };
    int ret;
    hash_test_init(test, sizeof(test), k, sizeof(k));
    /* Compute or check the reference siphash value */
    for (size_t i = 0; i < sizeof(test_lengths) / sizeof(size_t); i++) {
        if ((ret = siphash(test, test_lengths[i], k, sip_out, 8)) != 0) {
            DBG_PRINTF("Siphash l=%zu returns %d", test_lengths[i], ret);
            break;
        }
        else
        {
            uint64_t h = low_endian_64(sip_out);
            if (h != href[i]) {
                DBG_PRINTF("H[%zu] = %" PRIu64 "instead of %"PRIu64, i, h, href[i]);
#if COMPUTING_REFERENCE_SIPASH_VALUE
                href[i] = h;
#else
                ret = -1;
                break;
#endif
            }
        }
    }
#ifdef COMPARING_TIMES
    /* Compare execution time */
    uint64_t h;
    double sip_t[48];
    double basic_t[48];
    for (size_t lt=1; lt <= 48; lt++) {
        uint64_t start_siphash = picoquic_current_time();
        uint64_t siphash_sum = 0;
        uint64_t basic_sum = 0;
        size_t n = 0;

        for (size_t i = 0; i + lt < sizeof(test); i++) {
            (void)siphash(test, lt, k, (uint8_t*)&h, 8);
            siphash_sum += h;
            n++;
        }
        uint64_t start_basic = picoquic_current_time();
        for (size_t i = 0; i + lt < sizeof(test); i++) {
            h = picohash_bytes(test, (uint32_t)lt, k);
            basic_sum += h;
        }
        uint64_t end_basic = picoquic_current_time();
        uint64_t siphash_time = start_basic - start_siphash;
        uint64_t basic_time = end_basic - start_basic;
        double siphash_one = ((double)siphash_time) / n;
        double basic_one = ((double)basic_time) / n;
        sip_t[lt - 1] = siphash_one;
        basic_t[lt - 1] = basic_one;
        printf("Sip hash time, %zu: %" PRIu64 ", sum: %" PRIu64", n = % zu, us=%f\n", lt, siphash_time, siphash_sum, n, siphash_one);
        printf("Basic hash time, %zu: %" PRIu64 ", sum: %" PRIu64", n = % zu, us=%f\n", lt, basic_time, basic_sum, n, basic_one);
    }
    for (int i = 0; i < 48; i++) {
        printf("%d, %f, %f\n", i + 1, basic_t[i], sip_t[i]);
    }
#endif /* COMPARING TIMES */
    return ret;
}
