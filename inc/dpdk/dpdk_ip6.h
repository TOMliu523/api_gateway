/*****************************************************************************
 * filename: dpdk_ip6.c
 * function:
 * description:
 ****************************************************************************/
#ifndef __DPDK_IP6_H__
#define __DPDK_IP6_H__

#include <string.h>

#include <rte_ip6.h>
#include <rte_ip_frag.h>

#include "macro.h"
#include "dpdk_ip.h"
#include "dpdk_type.h"
#include "dpdk_common.h"

#define DPDK_IP6_ADDR_UNSPEC RTE_IPV6_ADDR_UNSPEC
#define DPDK_IP6_ADDR_ZERO DPDK_IP6_ADDR_UNSPEC

#define DPDK_IP6_INIT(a, b, c, d, e, f, g, h) RTE_IPV6(a, b, c, d, e, f, g, h)
#define DPDK_IP6_UNSPEC() DPDK_IP6_INIT(0, 0, 0, 0, 0, 0, 0, 0)

#define DPDK_TX_IP_TX_IP6_CKSUM (RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV6)

#if defined(__ORDER_LITTLE_ENDIAN__)
#define DPDK_IP6_LOOKBACK_BYTE0 (uint64_t) 0
#define DPDK_IP6_LOOKBACK_BYTE1 (uint64_t) 0x1000000000000
#else
#define DPDK_IP6_LOOKBACK_BYTE0 (uint64_t) 0
#define DPDK_IP6_LOOKBACK_BYTE1 (uint64_t) 0x1
#endif // __ORDER_LITTLE_ENDIAN__

#define dpdk_ip6_hdr rte_ipv6_hdr
#define dpdk_ip6_addr rte_ipv6_addr
#define dpdk_ip6_frag_ext rte_ipv6_fragment_ext

#define DPDK_ETHER_IP6 RTE_ETHER_TYPE_IPV6

#define DPDK_IP6_MASK_MAX RTE_IPV6_MAX_DEPTH
#define DPDK_IP6_ADDR_SIZE RTE_IPV6_ADDR_SIZE

#define DPDK_IP6_ADDR0(addr) (*(uint64_t *)addr)
#define DPDK_IP6_ADDR1(addr) (*(uint64_t *)((void *)addr + sizeof(uint64_t)))

#define DPDK_U64_ZERO (0UL)

enum IP6_ADDR_TYPE {
    IP6_ADDR_UNICAST,
    IP6_ADDR_UNSPEC,
    IP6_ADDR_MULTICAST,
};

static INLINE struct dpdk_ip6_hdr *dpdk_pktmbuf_ip6_hdr(struct dpdk_mbuf *m)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_ip6_hdr *, sizeof(struct dpdk_eth_hdr));
}

static INLINE struct dpdk_ip6_frag_ext *dpdk_pktmbuf_ip6_frag_hdr(struct dpdk_ip6_hdr *hdr)
{
    return rte_ipv6_frag_get_ipv6_fragment_header(hdr);
}

static INLINE void dpdk_ip6_addr_unspec(struct dpdk_ip6_addr *addr)
{
    DPDK_IP6_ADDR0(addr) = 0UL;
    DPDK_IP6_ADDR1(addr) = 0UL;
}

static INLINE void dpdk_ip6_addr_to_uint128(__uint128_t *v, const struct dpdk_ip6_addr *addr)
{
    uint64_t first = 0;
    uint64_t second = 0;

    first = dpdk_be_to_cpu_64(*(uint64_t *)addr);
    second = dpdk_be_to_cpu_64(*(uint64_t *)((void *)addr + 8));

    *v = ((__uint128_t)first << 64) | ((__uint128_t)second);
}

static INLINE void dpdk_ip6_addr_to_next(struct dpdk_ip6_addr *to, __uint128_t *v)
{
    uint64_t first = 0;
    uint64_t second = 0;

    first = dpdk_cpu_to_be_64((uint64_t)(*v >> 64));
    second = dpdk_cpu_to_be_64((uint64_t)(*v));

    dpdk_memcpy(to, &first, sizeof(first));
    dpdk_memcpy((void *)to + sizeof(first), &second, sizeof(second));
}

static INLINE uint32_t dpdk_ip6_addr_diff(const struct dpdk_ip6_addr *addr0, const struct dpdk_ip6_addr *addr1)
{
    __uint128_t first = 0;
    __uint128_t second = 0;

    dpdk_ip6_addr_to_uint128(&first, addr0);
    dpdk_ip6_addr_to_uint128(&second, addr1);

    if (second - first + 1 >= UINT32_MAX) {
        return UINT32_MAX;
    } else {
        return second - first + 1;
    }
}

static INLINE void dpdk_ip6_addr_inc(struct dpdk_ip6_addr *next, const struct dpdk_ip6_addr *addr)
{
    __uint128_t first = 0;

    dpdk_ip6_addr_to_uint128(&first, addr);
    first += 1;
    dpdk_ip6_addr_to_next(next, &first);
}

static INLINE int dpdk_ip6_addr_cmp(const void *first, const void *second, size_t len)
{
    return memcmp(first, second, len);
}

static INLINE bool dpdk_ip6_addr_eq(const struct dpdk_ip6_addr *addr1, const struct dpdk_ip6_addr *addr2)
{
    return (DPDK_IP6_ADDR0(addr1) == DPDK_IP6_ADDR0(addr2) && DPDK_IP6_ADDR1(addr1) == DPDK_IP6_ADDR1(addr2));
}

static INLINE bool dpdk_ip6_addr_is_unspec(const struct dpdk_ip6_addr *addr)
{
    return (DPDK_IP6_ADDR0(addr) == 0UL && DPDK_IP6_ADDR1(addr) == 0UL);
}

static INLINE bool dpdk_ip6_addr_is_lookback(const struct dpdk_ip6_addr *addr)
{
    return (DPDK_IP6_ADDR0(addr) == 0 && DPDK_IP6_ADDR1(addr) == DPDK_IP6_LOOKBACK_BYTE1);
}

static INLINE bool dpdk_ip6_addr_is_mcast(const struct dpdk_ip6_addr *addr)
{
    return rte_ipv6_addr_is_mcast(addr);
}

static INLINE bool dpdk_ip6_addr_is_ucast(const struct dpdk_ip6_addr *addr)
{
    return ((addr->a[0] != 0xFF) && !(DPDK_IP6_ADDR0(addr) == 0 && DPDK_IP6_ADDR1(addr) == 0));
}

static INLINE bool dpdk_ip6_version_check(const struct dpdk_ip6_hdr *ip6hdr)
{
    return (!rte_ipv6_check_version(ip6hdr));
}

static INLINE enum IP6_ADDR_TYPE dpdk_ip6_addr_type(const struct dpdk_ip6_addr *addr)
{
    switch (addr->a[0]) {
    case 0xFF: return IP6_ADDR_MULTICAST;
    case 0x00:
        if (DPDK_IP6_ADDR0(addr) == DPDK_U64_ZERO && DPDK_IP6_ADDR1(addr) == DPDK_U64_ZERO) {
            return IP6_ADDR_UNSPEC;
        } else {
            return IP6_ADDR_UNICAST;
        }
        break;
    default: return IP6_ADDR_UNICAST;
    }
}

static INLINE uint8_t dpdk_ip6_addr_mask(const struct dpdk_ip6_addr *mask)
{
    return rte_ipv6_mask_depth(mask);
}

static INLINE void dpdk_ip6_addr_subnet(struct dpdk_ip6_addr *addr, int mask)
{
    rte_ipv6_addr_mask(addr, mask);
}

static INLINE bool dpdk_ip6_addr_subnet_eq(const struct dpdk_ip6_addr *first, const struct dpdk_ip6_addr *second, uint8_t mask)
{
    if (mask < DPDK_IP6_MASK_MAX) {
        unsigned int d = mask / CHAR_BIT;
        uint8_t uint8_mask = ~(UINT8_MAX >> (mask % CHAR_BIT));

        if ((first->a[d] ^ second->a[d]) & uint8_mask) {
            return false;
        }

        return memcmp(first, second, d);
    }

    return dpdk_ip6_addr_eq(first, second);
}

static INLINE void dpdk_ip6_to_eth_mcast(struct dpdk_mac *mac, const struct dpdk_ip6_addr *ip)
{
    rte_ether_mcast_from_ipv6(mac, ip);
}

static INLINE void dpdk_ip6_solnode_from_addr(struct dpdk_ip6_addr *sol, const struct dpdk_ip6_addr *ip)
{
    return rte_ipv6_solnode_from_addr(sol, ip);
}

static INLINE int dpdk_ip6_next_ext(const uint8_t *p, int next_proto, size_t *ext_len)
{
    return rte_ipv6_get_next_ext(p, next_proto, ext_len);
}

static INLINE struct dpdk_mbuf *dpdk_ip6_mbuf_reassemble(void *arg, struct dpdk_mbuf *mbuf, uint64_t tms,
                                                         struct dpdk_ip6_hdr *ip6hdr, void *ipfraghdr)
{
    struct dpdk_ip_frag_handle *handle = arg;

    return rte_ipv6_frag_reassemble_packet(handle->table, handle->queue, mbuf, tms, ip6hdr, ipfraghdr);
}

static INLINE int dpdk_ip6_mbuf_fragment(struct dpdk_mbuf *in, void *out[], uint16_t out_nb, uint16_t mtu, void *pool, void *indirect_pool)
{
    return rte_ipv6_fragment_packet(in, (struct dpdk_mbuf **)out, out_nb, mtu, pool, indirect_pool);
}

#endif // __DPDK_IP6_H__