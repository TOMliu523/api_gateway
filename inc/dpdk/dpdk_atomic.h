/*****************************************************************************
 * filename: dpdk_atomic.h
 * function:
 * description:
 *****************************************************************************/

#include <rte_atomic.h>

#include "macro.h"

#define dpdk_atomic16_t rte_atomic16_t
#define dpdk_atomic32_t rte_atomic32_t
#define dpdk_atomic64_t rte_atomic64_t
#define dpdk_int128_t rte_int128_t

static INLINE int dpdk_atomic16_cmpset(volatile uint16_t *dst, uint16_t exp, uint16_t src)
{
    return rte_atomic16_cmpset(dst, exp, src);
}

static INLINE uint16_t dpdk_atomic16_exchange(volatile uint16_t *dst, uint16_t val)
{
    return rte_atomic16_exchange(dst, val);
}

static INLINE int dpdk_atomic16_test_and_set(dpdk_atomic16_t *v)
{
    return rte_atomic16_test_and_set(v);
}

static INLINE int dpdk_atomic16_inc_and_test(dpdk_atomic16_t *v)
{
    return rte_atomic16_inc_and_test(v);
}

static INLINE int dpdk_atomic16_dec_and_test(dpdk_atomic16_t *v)
{
    return rte_atomic16_dec_and_test(v);
}

static INLINE void dpdk_atomic16_inc(dpdk_atomic16_t *v)
{
    rte_atomic16_inc(v);
}

static INLINE void dpdk_atomic16_dec(dpdk_atomic16_t *v)
{
    rte_atomic16_dec(v);
}

static INLINE int dpdk_atomic32_cmpset(volatile uint32_t *dst, uint32_t exp, uint32_t src)
{
    return rte_atomic32_cmpset(dst, exp, src);
}

static INLINE uint32_t dpdk_atomic32_exchange(volatile uint32_t *dst, uint32_t val)
{
    return rte_atomic32_exchange(dst, val);
}

static INLINE int dpdk_atomic32_test_and_set(dpdk_atomic32_t *v)
{
    return rte_atomic32_test_and_set(v);
}

static INLINE int dpdk_atomic32_inc_and_test(dpdk_atomic32_t *v)
{
    return rte_atomic32_inc_and_test(v);
}

static INLINE int dpdk_atomic32_dec_and_test(dpdk_atomic32_t *v)
{
    return rte_atomic32_dec_and_test(v);
}

static INLINE void dpdk_atomic32_inc(dpdk_atomic32_t *v)
{
    return rte_atomic32_inc(v);
}

static INLINE void dpdk_atomic32_dec(dpdk_atomic32_t *v)
{
    return rte_atomic32_dec(v);
}

static INLINE int dpdk_atomic64_cmpset(volatile uint64_t *dst, uint64_t exp, uint64_t src)
{
    return rte_atomic64_cmpset(dst, exp, src);
}

static INLINE uint64_t dpdk_atomic64_exchange(volatile uint64_t *dst, uint64_t val)
{
    return rte_atomic64_exchange(dst, val);
}

static INLINE void dpdk_atomic64_init(dpdk_atomic64_t *v)
{
    rte_atomic64_init(v);
}

static INLINE int64_t dpdk_atomic64_read(dpdk_atomic64_t *v)
{
    return rte_atomic64_read(v);
}

static INLINE void dpdk_atomic64_set(dpdk_atomic64_t *v, int64_t new_value)
{
    rte_atomic64_set(v, new_value);
}

static INLINE void dpdk_atomic64_add(dpdk_atomic64_t *v, int64_t inc)
{
    rte_atomic64_add(v, inc);
}

static INLINE void dpdk_atomic64_sub(dpdk_atomic64_t *v, int64_t dec)
{
    rte_atomic64_sub(v, dec);
}

static INLINE void dpdk_atomic64_inc(dpdk_atomic64_t *v)
{
    rte_atomic64_inc(v);
}

static INLINE void dpdk_atomic64_dec(dpdk_atomic64_t *v)
{
    rte_atomic64_dec(v);
}

static INLINE int64_t dpdk_atomic64_add_return(dpdk_atomic64_t *v, int64_t inc)
{
    return rte_atomic64_add_return(v, inc);
}

static INLINE int64_t dpdk_atomic64_sub_return(dpdk_atomic64_t *v, int64_t dec)
{
    return rte_atomic64_sub_return(v, dec);
}

static INLINE int dpdk_atomic64_inc_and_test(dpdk_atomic64_t *v)
{
    return rte_atomic64_inc_and_test(v);
}

static INLINE int dpdk_atomic64_dec_and_test(dpdk_atomic64_t *v)
{
    return rte_atomic64_dec_and_test(v);
}

static INLINE int dpdk_atomic64_test_and_set(dpdk_atomic64_t *v)
{
    return rte_atomic64_test_and_set(v);
}

static INLINE void dpdk_atomic64_clear(dpdk_atomic64_t *v)
{
    return rte_atomic64_clear(v);
}

static INLINE int dpdk_atomic128_cmp_exchange(dpdk_int128_t *dst, dpdk_int128_t *exp, const dpdk_int128_t *src,
                                              unsigned int weak, int success, int failure)
{
    return rte_atomic128_cmp_exchange(dst, exp, src, weak, success, failure);
}