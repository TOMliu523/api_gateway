/*****************************************************************************
 * filename: dpdk_ip.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_IP_H__
#define __DPDK_IP_H__

#include <stdbool.h>
#include <rte_ip_frag.h>

#include "macro.h"

#define dpdk_ip_frag_table rte_ip_frag_tbl
#define dpdk_ip_frag_death_queue rte_ip_frag_death_row

#define DPDK_IP_FRAG_RECALL_THOLD 1024

struct dpdk_ip_frag_handle {
    uint32_t continue_fail_thold;
    uint64_t add_mbuf_count;
    uint64_t sub_mbuf_count;
    struct dpdk_ip_frag_table *table;
    struct dpdk_ip_frag_death_queue *queue;
};

extern void *dpdk_ip_frag_table_create(uint64_t max_cycles, int hw_numa_id);
extern void dpdk_ip_frag_table_destroy(void *handle);

static INLINE bool dpdk_ip_mbuf_fail_exceed_thold(const struct dpdk_ip_frag_handle *handle)
{
    return (handle->add_mbuf_count >= handle->sub_mbuf_count + DPDK_IP_FRAG_RECALL_THOLD);
}

static INLINE void dpdk_ip_mbuf_recall(struct dpdk_ip_frag_handle *handle, uint64_t tms)
{
    rte_ip_frag_table_del_expired_entries(handle->table, handle->queue, tms);

    if (handle->queue->cnt != 0) {
        rte_ip_frag_free_death_row(handle->queue, 8);
        handle->sub_mbuf_count += handle->queue->cnt;
    }
}

static INLINE bool dpdk_ip_mbuf_need_recall(struct dpdk_ip_frag_handle *handle)
{
    return (handle->add_mbuf_count != handle->sub_mbuf_count);
}

static INLINE void dpdk_ip_mbuf_table_add(struct dpdk_ip_frag_handle *handle, int n)
{
    handle->add_mbuf_count += n;
}

static INLINE void dpdk_ip_mbuf_table_sub(struct dpdk_ip_frag_handle *handle, int n)
{
    handle->sub_mbuf_count -= n;
}

static INLINE void dpdk_ip_reassemble_finish(void *handle, int nb)
{
    dpdk_ip_mbuf_table_sub(handle, nb);
}

static INLINE void dpdk_ip_reassemble_pending(void *handle, uint64_t cycles)
{
    dpdk_ip_mbuf_table_add(handle, 1);
    if (dpdk_ip_mbuf_fail_exceed_thold(handle)) {
        dpdk_ip_mbuf_recall(handle, cycles);
    }
}

#endif // __DPDK_IP_H__