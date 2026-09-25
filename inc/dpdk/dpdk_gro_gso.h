/*****************************************************************************
 * filename: dpdk_gro_gso.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_GRO_GSO_H__
#define __DPDK_GRO_GSO_H__

#include <rte_gro.h>
#include <rte_gso.h>

#include "macro.h"
#include "dpdk_type.h"

#define dpdk_gro_ctx rte_gro_param
#define dpdk_gso_ctx rte_gso_ctx

static INLINE uint16_t
dpdk_gro_reassemble(struct dpdk_mbuf *ms[], uint16_t n)
{
    struct dpdk_gro_ctx ctx = {
        .gro_types = RTE_GRO_TCP_IPV4 | RTE_GRO_UDP_IPV4,
        .max_flow_num = 4096,
        .max_item_per_flow = 64,
        .socket_id = (uint16_t) SOCKET_ID_ANY,
    };

    return rte_gro_reassemble_burst(ms, n, &ctx);
}

static INLINE int
dpdk_gso_segment(struct dpdk_mbuf *m, struct dpdk_mbuf *out[], uint16_t n,
                 void *direct_pool, void *indirect_pool, uint16_t size)
{
    struct dpdk_gso_ctx ctx = {
        .direct_pool = direct_pool,
        .indirect_pool = indirect_pool,
        .flag = 0,
        .gso_types = RTE_ETH_TX_OFFLOAD_TCP_TSO,
        .gso_size = size,
    };

    return rte_gso_segment(m, &ctx, out, n);
}

#endif // __DPDK_GRO_GSO_H__