/*****************************************************************************
 * filename: dpdk_bitmap.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_BITMAP_H__
#define __DPDK_BITMAP_H__

#include <rte_bitmap.h>

#include "macro.h"

#define dpdk_bitmap rte_bitmap

extern struct dpdk_bitmap *dpdk_bitmap_init(uint32_t bit_count, int hw_numa_id);
extern struct dpdk_bitmap *dpdk_bitmap_init_with_all_set(uint32_t bit_count, int hw_numa_id);
extern void dpdk_bitmap_fini(struct dpdk_bitmap *bitmap);

static INLINE uint32_t dpdk_bitmap_memory_calc(uint32_t bit_count)
{
    return rte_bitmap_get_memory_footprint(bit_count);
}

static INLINE void dpdk_bitmap_reset(struct dpdk_bitmap *bitmap)
{
    rte_bitmap_reset(bitmap);
}

static INLINE void dpdk_bitmap_prefetch(struct dpdk_bitmap *bitmap, uint32_t pos)
{
    rte_bitmap_prefetch0(bitmap, pos);
}

static INLINE uint64_t dpdk_bitmap_get(struct dpdk_bitmap *bitmap, uint32_t pos)
{
    return rte_bitmap_get(bitmap, pos);
}

static INLINE void dpdk_bitmap_set(struct dpdk_bitmap *bitmap, uint32_t pos)
{
    return rte_bitmap_set(bitmap, pos);
}

static INLINE void dpdk_bitmap_clear(struct dpdk_bitmap *bitmap, uint32_t pos)
{
    rte_bitmap_clear(bitmap, pos);
}

static INLINE void dpdk_bitmap_set_slab(struct dpdk_bitmap *bitmap, uint32_t pos, uint64_t slab)
{
    return rte_bitmap_set_slab(bitmap, pos, slab);
}

static INLINE int dpdk_bitmap_scan(struct dpdk_bitmap *bitmap, uint32_t *pos, uint64_t *slab)
{
    return rte_bitmap_scan(bitmap, pos, slab);
}

#endif // __DPDK_BITMAP_H__