/*****************************************************************************
 * filename: dpdk_ip6.c
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_IP6_H__
#define __DPDK_IP6_H__

#include <linux/icmpv6.h>

#include <rte_ip6.h>

#include "macro.h"
#include "dpdk_type.h"

#define DPDK_IP6_ADDR_UNSPEC RTE_IPV6_ADDR_UNSPEC
#define DPDK_IP6_ADDR_ZERO dpdk_ip6_addr_UNSPEC

#define DPDK_IP6_INIT(a, b, c, d, e, f, g, h) RTE_IPV6(a, b, c, d, e, f, g, h)
#define DPDK_IP6_UNSPEC() dpdk_ip6_INIT(0, 0, 0, 0, 0, 0, 0, 0)

#if defined(__ORDER_LITTLE_ENDIAN__)
#define DPDK_IP6_LOOKBACK_BYTE0 (uint64_t) 0
#define DPDK_IP6_LOOKBACK_BYTE1 (uint64_t) 0x1000000000000
#else
#define DPDK_IP6_LOOKBACK_BYTE0 (uint64_t) 0
#define DPDK_IP6_LOOKBACK_BYTE1 (uint64_t) 0x1
#endif // __ORDER_LITTLE_ENDIAN__

#define dpdk_ip6_hdr rte_ipv6_hdr

#define DPDK_IP6_MASK_MAX RTE_IPV6_MAX_DEPTH
#define DPDK_IP6_ADDR_SIZE RTE_IPV6_ADDR_SIZE
#define DPDK_ICMP6_MBUF_LEN_MIN (dpdk_ip6_MBUF_LEN_MIN + sizeof(struct dpdk_icmp6))

/**
 * Note (alignment requirements for big_addr):
 *  - big_addr is an __uint128_t and MUST be read/written only at a 16-byte aligned address.
 *  - Dereferencing an __uint128_t* at an unaligned address is undefined behavior in C/C++:
 *      * some architectures raise alignment faults/SIGBUS;
 *      * 128-bit atomicity is not guaranteed (torn loads/stores);
 *      * even on x86 it may still run but can become much slower, especially when crossing
 *        cache-line or page boundaries.
 *  - Therefore, use big_addr only when the object itself is declared with alignas(16)
 *    (or the compiler attribute aligned(16)), or after verifying 16-byte alignment with
 *    RTE_IS_ALIGNED(ptr, 16) (or `(uintptr_t)ptr % 16 == 0`).
 *  - For wire/packed data that may be unaligned, use one of the safe alternatives:
 *      * compare/copy via int_addr[2] (two uint64_t values);
 *      * memcpy into an aligned temporary, then operate on __uint128_t;
 *      * or use SIMD unaligned loads (e.g., x86 _mm_loadu_si128).
 *
 * Example:
 *    const union dpdk_ip6_addr *p = ...;
 *    __uint128_t v;
 *    if (RTE_IS_ALIGNED(p, 16)) {
 *        v = p->big_addr;                // aligned: OK
 *    } else {
 *        // unaligned: memcpy first; the compiler will emit a safe unaligned load
 *        __builtin_memcpy(&v, p->addr.s6_addr, 16);
 *    }
 *
 * For 128-bit atomic operations (CAS, etc.) you must ALSO guarantee 16-byte alignment;
 * otherwise hardware atomics may be unavailable or behavior is undefined.
 */
union dpdk_ip6_addr {
    struct rte_ipv6_addr addr;
    uint64_t addr8[2];
    /* use only when strictly 16-byte aligned */
    __uint128_t addr16;
};

static INLINE struct dpdk_ip6_hdr *dpdk_pktmbuf_ip6_hdr(struct dpdk_mbuf *m)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_ip6_hdr *, sizeof(struct dpdk_eth));
}

static INLINE void dpdk_ip6_addr_unspec(union dpdk_ip6_addr *addr)
{
    addr->addr8[0] = 0;
    addr->addr8[1] = 0;
}

static INLINE bool dpdk_ip6_addr_eq(const union dpdk_ip6_addr *addr1, const union dpdk_ip6_addr *addr2)
{
    return (addr1->addr8[0] == addr2->addr8[0]) && (addr1->addr8[1] == addr2->addr8[1]);
}

static INLINE bool dpdk_ip6_addr_is_unspec(const union dpdk_ip6_addr *addr)
{
    return (addr->addr8[0] == 0) && (addr->addr8[1] == 0);
}

static INLINE bool dpdk_ip6_addr_is_lookback(const union dpdk_ip6_addr *addr)
{
    return (addr->addr8[0] == 0) && (addr->addr8[1] == DPDK_IP6_LOOKBACK_BYTE1);
}

static INLINE bool dpdk_ip6_addr_is_mcast(const union dpdk_ip6_addr *addr)
{
    return rte_ipv6_addr_is_mcast(&addr->addr);
}

static INLINE uint8_t dpdk_ip6_addr_mask(const union dpdk_ip6_addr *mask)
{
    return rte_ipv6_mask_depth(&mask->addr);
}

static INLINE void dpdk_ip6_addr_subnet(union dpdk_ip6_addr *addr, int mask)
{
    rte_ipv6_addr_mask(&addr->addr, mask);
}

static INLINE bool dpdk_ip6_addr_subnet_eq(const union dpdk_ip6_addr *first, const union dpdk_ip6_addr *second, uint8_t mask)
{
    if (mask < DPDK_IP6_MASK_MAX) {
        unsigned int d = mask / CHAR_BIT;
        uint8_t uint8_mask = ~(UINT8_MAX >> (mask % CHAR_BIT));

        if (first->addr.a[d] ^ second->addr.a[d] & uint8_mask) {
            return false;
        }

        return memcmp(&first->addr, &second->addr, d);
    }

    return dpdk_ip6_addr_eq(first, second);
}

#endif // __DPDK_IP6_H__