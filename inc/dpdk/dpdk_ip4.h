/*****************************************************************************
 * filename: dpdk_ip4.h
 * function:
 * description:
 *****************************************************************************/
#pragma once

#ifndef __DPDK_IP4_H__
#define __DPDK_IP4_H__

#include <stdbool.h>

#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_cksum.h>

#include "dpdk_ip.h"
#include "dpdk_type.h"

#define dpdk_ip4_hdr rte_ipv4_hdr

#define DPDK_ETHER_IP4 RTE_ETHER_TYPE_IPV4

#define DPDK_IP_RX_CKSUM_MASK RTE_MBUF_F_RX_IP_CKSUM_MASK
#define DPDK_IP_RX_CKSUM_UNKNOWN RTE_MBUF_F_RX_L4_CKSUM_UNKNOWN
#define DPDK_IP_RX_CKSUM_GOOD RTE_MBUF_F_RX_IP_CKSUM_GOOD
#define DPDK_IP_RX_CKSUM_BAD RTE_MBUF_F_RX_IP_CKSUM_BAD
#define DPDK_IP_RX_CKSUM_NONE RTE_MBUF_F_RX_L4_CKSUM_NONE

#define DPDK_IP_TX_IP_CKSUM (RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4)

#define DPDK_ETHER_TYPE_IPV4 RTE_ETHER_TYPE_IPV4

#define dpdk_ip4_HDR_MF_SHIFT RTE_IPV4_HDR_MF_SHIFT
#define dpdk_ip4_HDR_DF_SHIFT RTE_IPV4_HDR_DF_SHIFT

static INLINE struct dpdk_ip4_hdr *dpdk_pktmbuf_ip4_hdr(struct dpdk_mbuf *m)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_ip4_hdr *, sizeof(struct dpdk_eth_hdr));
}

static INLINE uint8_t dpdk_ip4_header_len(const struct dpdk_ip4_hdr *ip4hdr)
{
    return rte_ipv4_hdr_len(ip4hdr);
}

static INLINE bool dpdk_ip4_header_cksum_verify(const struct dpdk_mbuf *mbuf, const struct dpdk_ip4_hdr *ip4hdr)
{
    switch (mbuf->ol_flags & DPDK_IP_RX_CKSUM_MASK) {
    case DPDK_IP_RX_CKSUM_GOOD: return true;
    case DPDK_IP_RX_CKSUM_BAD: return false;
    default: return (rte_raw_cksum(ip4hdr, dpdk_ip4_header_len(ip4hdr)) == 0xFFFF);
    }
}

static INLINE void dpdk_ip4_cksum(struct dpdk_ip4_hdr *ip4hdr)
{
    ip4hdr->hdr_checksum = 0;
    ip4hdr->hdr_checksum = rte_ipv4_cksum(ip4hdr);
}

static INLINE bool dpdk_ip4_mbuf_is_fragmented(const struct dpdk_ip4_hdr *ip4hdr)
{
    return rte_ipv4_frag_pkt_is_fragmented(ip4hdr);
}

static INLINE struct dpdk_mbuf *dpdk_ip4_mbuf_reassemble(void *arg, struct dpdk_mbuf *mbuf, uint64_t tms, struct dpdk_ip4_hdr *ip4hdr)
{
    struct dpdk_ip_frag_handle *handle = arg;

    return rte_ipv4_frag_reassemble_packet(handle->table, handle->queue, mbuf, tms, ip4hdr);
}

static INLINE int dpdk_ip4_mbuf_fragment(struct dpdk_mbuf *in, void *out[], uint16_t out_nb, uint16_t mtu, void *pool, void *indirect_pool)
{
    return rte_ipv4_fragment_packet(in, (struct dpdk_mbuf **)out, out_nb, mtu, pool, indirect_pool);
}

#endif // __DPDK_IP4_H__