/*****************************************************************************
 * filename: dpdk_bitops.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_BITOPS_H__
#define __DPDK_BITOPS_H__

#include <rte_bitops.h>

#include "macro.h"

#define dpdk_bit_u32(n) RTE_BIT32(n)
#define dpdk_bit_u64(n) RTE_BIT64(n)

#define dpdk_shift_u32(v, n) RTE_SHIFT_VAL32(v, n)
#define dpdk_shift_u64(v, n) RTE_SHIFT_VAL64(v, n)

#define dpdk_gencomnmask_u32(high, low) RTE_GENMASK32(high, low)
#define dpdk_gencomnmask_u64(high, low) RTE_GENMASK32(high, low)

// reg = 0x12345678 mask = 0x00FF0000 result = 0x34
#define dpdk_filed_get_u32(mask, reg) RTE_FIELD_GET32(mask, reg)
#define dpdk_filed_get_u64(mask, reg) RTE_FIELD_GET64(mask, reg)

/**
 * A unified set of bit manipulation macros built on top of DPDK's rte_bit_* APIs.
 *
 * These macros provide convenient wrappers to operate on individual bits
 * in 32-bit or 64-bit words. The actual implementation is selected
 * automatically based on the type of the pointer argument.
 *
 * - dpdk_bit_test(v, n)
 *     Test if bit n is set (returns non-zero if bit is '1').
 *
 * - dpdk_bit_set(v, n)
 *     Set bit n to '1'.
 *
 * - dpdk_bit_clear(v, n)
 *     Clear bit n to '0'.
 *
 * - dpdk_bit_assign(v, n, o)
 *     Assign bit n to the value o (true → '1', false → '0').
 *
 * - dpdk_bit_flip(v, n)
 *     Flip bit n (toggle between '0' and '1').
 *
 * Notes:
 * - These macros are thin wrappers around the rte_bit_* functions provided by DPDK.
 * - They support both uint32_t and uint64_t words, including volatile variants.
 * - No guarantees are provided regarding memory ordering or atomicity.
 *   Use external synchronization or atomic primitives if multiple threads
 *   may modify the same word concurrently.
 */
static INLINE unsigned int dpdk_bit_test_u32(uint32_t x, int n)
{
    return ((x >> n) & 0x1);
}

static INLINE unsigned int dpdk_bit_test_u64(uint64_t x, int n)
{
    return ((x >> n) & 0x1);
}

static INLINE uint32_t dpdk_bit_set_u32(uint32_t x, int n)
{
    return x |= dpdk_bit_u32(n);
}

static INLINE uint64_t dpdk_bit_set_u64(uint64_t x, int n)
{
    return x |= dpdk_bit_u64(n);
}

static INLINE uint32_t dpdk_bit_clear_u32(uint32_t x, int n)
{
    return x &= ~dpdk_bit_u32(n);
}

static INLINE uint64_t dpdk_bit_clear_u64(uint64_t x, int n)
{
    return x &= ~dpdk_bit_u64(n);
}

static INLINE uint32_t dpdk_bit_flip_u32(uint32_t x, int n)
{
    return x ^= dpdk_bit_u32(n);
}

static INLINE uint64_t dpdk_bit_flip_u64(uint64_t x, int n)
{
    return x ^= dpdk_bit_u64(n);
}

static INLINE unsigned int dpdk_clz32(uint32_t x)
{
    return rte_clz32(x);
}

static INLINE unsigned int dpdk_clz64(uint64_t x)
{
    return rte_clz64(x);
}

static INLINE unsigned int dpdk_ctz32(uint32_t x)
{
    return rte_ctz32(x);
}

static INLINE unsigned int dpdk_ctz64(uint64_t x)
{
    return rte_ctz64(x);
}

static INLINE unsigned int dpdk_popcount32(uint32_t x)
{
    return rte_popcount32(x);
}

static INLINE unsigned int dpdk_popcount64(uint64_t x)
{
    return rte_popcount64(x);
}

static INLINE unsigned int dpdk_comb32ms1b(uint32_t x)
{
    return rte_combine32ms1b(x);
}

static INLINE unsigned int dpdk_comb64ms1b(uint64_t x)
{
    return rte_combine64ms1b(x);
}

static INLINE unsigned int dpdk_is_power_of_2(uint32_t x)
{
    return rte_is_power_of_2(x);
}

static INLINE unsigned int dpdk_next_32_pow2(uint32_t x)
{
    return rte_align32pow2(x);
}

static INLINE uint64_t dpdk_next_64_pow2(uint64_t x)
{
    return rte_align64pow2(x);
}

static INLINE unsigned int dpdk_prev_32_pow2(uint32_t x)
{
    return rte_align32prevpow2(x);
}

static INLINE uint64_t dpdk_prev_64_pow2(uint64_t x)
{
    return rte_align64prevpow2(x);
}

static INLINE uint32_t dpdk_round_up_log2_u32(uint32_t x)
{
    return rte_log2_u32(x);
}

static INLINE uint64_t dpdk_round_up_log2_u64(uint64_t x)
{
    return rte_log2_u64(x);
}

#endif // __DPDK_BITOPS_H__