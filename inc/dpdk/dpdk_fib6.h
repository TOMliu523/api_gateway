/*****************************************************************************
 * filename: dpdk_fib6.h
 * function:
 * description: High-Performance Longest Prefix Match Library
 ****************************************************************************/

#ifndef __DPDK_FIB6_H__
#define __DPDK_FIB6_H__

#include <rte_fib6.h>

#include "dpdk_ip6.h"

#define dpdk_fib6 rte_fib6

#define DPDK_FIB6_DEFAULT 1000000000

// create or destroy
extern struct dpdk_fib6 *dpdk_fib6_create(int hw_numa_id, int max_item);
extern void dpdk_fib6_destroy(struct dpdk_fib6 *fib);

static INLINE int dpdk_fib6_add(struct dpdk_fib6 *fib, const union dpdk_ip6_addr *ip6, uint8_t mask, uint64_t next_hop)
{
    return rte_fib6_add(fib, (const struct rte_ipv6_addr *)ip6, mask, next_hop);
}

static INLINE int dpdk_fib6_del(struct dpdk_fib6 *fib, const union dpdk_ip6_addr *ip6, uint8_t mask)
{
    return rte_fib6_delete(fib, (const struct rte_ipv6_addr *)ip6, mask);
}

static INLINE int dpdk_fib6_lookup(struct dpdk_fib6 *fib, const union dpdk_ip6_addr ip6[], uint64_t next_hop[], int n)
{
    return rte_fib6_lookup_bulk(fib, (const struct rte_ipv6_addr *)ip6, next_hop, n);
}

#endif // __DPDK_FIB6_H__