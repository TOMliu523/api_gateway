/*****************************************************************************
 * filename: dpdk_bitmap.c
 * function:
 * description:
 *****************************************************************************/

#include "log.h"
#include "dpdk_bitmap.h"
#include "dpdk_common.h"

struct dpdk_bitmap *dpdk_bitmap_init(uint32_t bit_count, int hw_numa_id)
{
    uint32_t size = 0;
    uint8_t *bitmap = NULL;

    size = rte_bitmap_get_memory_footprint(bit_count);
    bitmap = dpdk_malloc_numa(size, hw_numa_id);
    if (bitmap == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    rte_bitmap_init(bit_count, bitmap, size);
    rte_bitmap_init_with_all_set(bit_count, bitmap, size);
}

void dpdk_bitmap_fini(struct dpdk_bitmap *bitmap)
{
    rte_bitmap_free(bitmap);
}