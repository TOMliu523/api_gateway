/*****************************************************************************
 * filename: dpdk_fib.h
 * function:
 * description: High-Performance Longest Prefix Match Library
 ****************************************************************************/

#ifndef __DPDK_ROUTE_H__
#define __DPDK_ROUTE_H__

#include <rte_fib.h>

#include "macro.h"

// type
#define dpdk_fib rte_fib
#define dpdk_rib rte_rib

#define DPDK_FIB_DEFAULT 1000000000

// create or destriy
extern struct dpdk_fib *dpdk_fib_create(const char *name, int hw_numa_id, int max_item);

static INLINE struct dpdk_fib *dpdk_fib_existing(const char *name)
{
    return rte_fib_find_existing(name);
}

static INLINE void dpdk_fib_destroy(struct dpdk_fib *fib)
{
    return rte_fib_free(fib);
}

// add/delete/update/lookup
static INLINE int dpdk_fib_add(struct dpdk_fib *fib, uint32_t ip, uint8_t mask, uint64_t next_hop)
{
    return rte_fib_add(fib, ip, mask, next_hop);
}

static INLINE int dpdk_fib_del(struct dpdk_fib *fib, uint32_t ip, uint8_t mask)
{
    return rte_fib_delete(fib, ip, mask);
}

static INLINE int dpdk_fib_lookup(struct dpdk_fib *fib, uint32_t ip[], uint64_t next_hops[], int n)
{
    return rte_fib_lookup_bulk(fib, ip, next_hops, n);
}

#endif // __DPDK_ROUTE_H__