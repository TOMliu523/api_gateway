/*****************************************************************************
 * filename: dpdk_ipv4.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_IPV4_H__
#define __DPDK_IPV4_H_

#include <stdbool.h>

#include <rte_cksum.h>

#include "dpdk_type.h"

#define DPDK_RX_IP_CKSUM_GOOD RTE_MBUF_F_RX_L4_CKSUM_GOOD

#define DPDK_TX_IP_TX_IP_CKSUM (RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4)

#define DPDK_ETHER_TYPE_IPV4 RTE_ETHER_TYPE_IPV4

#define DPDK_IPV4_HDR_MF_SHIFT RTE_IPV4_HDR_MF_SHIFT
#define DPDK_IPV4_HDR_DF_SHIFT RTE_IPV4_HDR_DF_SHIFT

static INLINE struct dpdk_ipv4 *dpdk_pktmbuf_ipv4(struct dpdk_mbuf *m)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_ipv4 *, sizeof(struct dpdk_eth));
}

static INLINE struct dpdk_ipv4 *dpdk_pktmbuf_frag_ipv4(struct dpdk_mbuf *mbuf)
{
    return rte_pktmbuf_mtod(mbuf, struct dpdk_ipv4*);
}

static INLINE uint8_t dpdk_ipv4_header_len(const struct dpdk_ipv4 *ipv4)
{
    return rte_ipv4_hdr_len(ipv4);
}

static INLINE bool dpdk_ipv4_cksum_verify(const struct dpdk_ipv4 *ipv4)
{
    return (rte_ipv4_cksum(ipv4) == 0);
}

static INLINE void dpdk_ipv4_cksum(struct dpdk_ipv4 *ipv4)
{
    ipv4->hdr_checksum = 0;
    ipv4->hdr_checksum = rte_ipv4_cksum(ipv4);
}

#endif // __DPDK_IPV4_H__