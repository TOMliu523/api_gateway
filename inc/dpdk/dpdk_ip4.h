/*****************************************************************************
 * filename: dpdk_ip4.h
 * function:
 * description:
 *****************************************************************************/
#pragma once

#ifndef __DPDK_IP4_H__
#define __DPDK_IP4_H_

#include <stdbool.h>

#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_cksum.h>

#include "dpdk_type.h"

#define dpdk_ip4_hdr rte_ipv4_hdr

#define DPDK_RX_IP_CKSUM_GOOD RTE_MBUF_F_RX_L4_CKSUM_GOOD

#define DPDK_TX_IP_TX_IP_CKSUM (RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4)

#define DPDK_ETHER_TYPE_IPV4 RTE_ETHER_TYPE_IPV4

#define dpdk_ip4_HDR_MF_SHIFT RTE_IPV4_HDR_MF_SHIFT
#define dpdk_ip4_HDR_DF_SHIFT RTE_IPV4_HDR_DF_SHIFT

static INLINE struct dpdk_ip4_hdr *dpdk_pktmbuf_ip4_hdr(struct dpdk_mbuf *m)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_ip4_hdr *, sizeof(struct dpdk_eth));
}

static INLINE uint8_t dpdk_ip4_header_len(const struct dpdk_ip4_hdr *ip4hdr)
{
    return rte_ipv4_hdr_len(ip4hdr);
}

static INLINE bool dpdk_ip4_header_cksum_verify(const struct dpdk_ip4_hdr *ip4hdr)
{
    return (rte_ipv4_cksum(ip4hdr) == 0);
}

static INLINE void dpdk_ip4_cksum(struct dpdk_ip4_hdr *ip4hdr)
{
    ip4hdr->hdr_checksum = 0;
    ip4hdr->hdr_checksum = rte_ipv4_cksum(ip4hdr);
}

#endif // __DPDK_IP4_H__