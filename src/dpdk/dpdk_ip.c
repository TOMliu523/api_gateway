/*****************************************************************************
 * filename: dpdk_ipv4.c
 * function:
 * description:
 ****************************************************************************/

#include <rte_ip_frag.h>

#include "log.h"
#include "dpdk_ip.h"
#include "dpdk_common.h"

#define DPDK_IP_FRAG_BUCKET_NUM 4096
#define DPDK_IP_FRAG_BUCKET_ENTRIES 32
#define DPDK_IP_FRAG_MAX_ENTRIES 102400

static __thread struct dpdk_ip_frag_handle tls_handle = {
    .add_mbuf_count = 0,
    .sub_mbuf_count = 0,
};

void *dpdk_ip_frag_table_create(uint64_t max_cycles, int hw_numa_id)
{
    struct dpdk_ip_frag_table *table = NULL;
    struct dpdk_ip_frag_death_queue *queue = NULL;

    table = rte_ip_frag_table_create(DPDK_IP_FRAG_BUCKET_NUM,
                                     DPDK_IP_FRAG_BUCKET_ENTRIES,
                                     DPDK_IP_FRAG_MAX_ENTRIES,
                                     2 * max_cycles,
                                     hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        LOG_ERROR("Failure rte_ip_frag_table_create: %s", strerror(rte_errno));
        return NULL;
    }

    queue = dpdk_malloc_numa(sizeof(*queue), hw_numa_id);
    if (UNLIKELY(queue == NULL)) {
        LOG_ERROR("Failure dpdk_malloc_numa: %s", strerror(rte_errno));
        rte_ip_frag_table_destroy(table);
        return NULL;
    }

    memset(queue->row, 0, sizeof(*queue));

    tls_handle.table = table;
    tls_handle.queue = queue;

    return &tls_handle;
}

void dpdk_ip_frag_table_destroy(void *table)
{
    if (table != NULL) {
        struct dpdk_ip_frag_handle *handle = table;

        rte_ip_frag_table_destroy(handle->table);
        dpdk_free(handle->queue);
    }
}