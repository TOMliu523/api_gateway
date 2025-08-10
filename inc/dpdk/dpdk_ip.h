/*****************************************************************************
 * filename: dpdk_ip.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_IP_H__
#define __DPDK_IP_H__

#include <stdbool.h>
#include <rte_ip_frag.h>

#include "dpdk_ip4.h"
#include "dpdk_ip6.h"

#define dpdk_ip_frag_table rte_ip_frag_tbl
#define dpdk_ip_frag_death_queue rte_ip_frag_death_row

#define DPDK_IP_FRAG_RECALL_THOLD 1024

#define DPDK_PTYPE_IP4 RTE_PTYPE_L3_IPV4
#define DPDK_PTYPE_IP6 RTE_PTYPE_L3_IPV6
#define DPDK_PTYPE_L3_TYPE(type) ((type) & RTE_PTYPE_L3_MASK)

struct dpdk_ip_frag_handle {
    uint32_t continue_fail_thold;
    uint64_t add_mbuf_count;
    uint64_t sub_mbuf_count;
    struct dpdk_ip_frag_table *table;
    struct dpdk_ip_frag_death_queue *queue;
};

union dpdk_ip {
    uint32_t ip4;
    union dpdk_ip6_addr ip6;
};

extern void *dpdk_ip_frag_table_create(uint64_t max_cycles, int hw_numa_id);
extern void dpdk_ip_frag_table_destroy(void *handle);

static INLINE bool dpdk_ip4_mbuf_is_fragmented(const struct dpdk_ip4_hdr *ip4hdr)
{
    return rte_ipv4_frag_pkt_is_fragmented(ip4hdr);
}

static INLINE struct dpdk_mbuf *dpdk_ip4_mbuf_reassemble(void *arg, struct dpdk_mbuf *mb, uint64_t tms, struct dpdk_ip4_hdr *ip4hdr)
{
    struct dpdk_ip_frag_handle *handle = arg;

    return rte_ipv4_frag_reassemble_packet(handle->table, handle->queue, mb, tms, ip4hdr);
}

static INLINE int dpdk_ip4_mbuf_fragment(struct dpdk_mbuf *in, void *out[], uint16_t out_nb, uint16_t mtu, void *pool, void *indirect_pool)
{
    return rte_ipv4_fragment_packet(in, (struct dpdk_mbuf **)out, out_nb, mtu, pool, indirect_pool);
}

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

static INLINE void dpdk_ip_mbuf_table_add(struct dpdk_ip_frag_handle *handle)
{
    handle->add_mbuf_count += 1;
}

static INLINE void dpdk_ip_mbuf_table_sub(struct dpdk_ip_frag_handle *handle)
{
    handle->sub_mbuf_count += 1;
}

#endif // __DPDK_IP_H__