/*****************************************************************************
 * filename: dpdk_tcp.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_TCP_H__
#define __DPDK_TCP_H__

#include <rte_ip4.h>
#include <rte_tcp.h>
#include <rte_gro.h>
#include <rte_mbuf_core.h>

#include "dpdk_ip4.h"
#include "dpdk_type.h"

#define dpdk_tcp_hdr rte_tcp_hdr

#define DPDK_TCP_FIN_F RTE_TCP_FIN_FLAG
#define DPDK_TCP_SYN_F RTE_TCP_SYN_FLAG
#define DPDK_TCP_RST_F RTE_TCP_RST_FLAG
#define DPDK_TCP_PSH_F RTE_TCP_PSH_FLAG
#define DPDK_TCP_ACK_F RTE_TCP_ACK_FLAG
// URG Not Need Support
#define DPDK_TCP_ECE_F RTE_TCP_ECE_FLAG
#define DPDK_TCP_CWR_F RTE_TCP_CWR_FLAG

// cksum
#define DPDK_TCP_RX_CKSUM_MASK RTE_MBUF_F_RX_L4_CKSUM_MASK

#define DPDK_TCP_RX_CKSUM_UNKNOWN 0
#define DPDK_TCP_RX_CKSUM_BAD RTE_MBUF_F_RX_L4_CKSUM_BAD
#define DPDK_TCP_RX_CKSUM_GOOD RTE_MBUF_F_RX_L4_CKSUM_GOOD
#define DPDK_TCP_RX_CKSUM_NONE RTE_MBUF_F_RX_L4_CKSUM_NONE

#define DPDK_TCP_TX_CKSUM RTE_MBUF_F_TX_TCP_CKSUM

static INLINE void dpdk_tcp4_mbuf_cksum(struct dpdk_mbuf *m, const struct dpdk_ip4_hdr *ip4hdr)
{
    struct dpdk_tcp_hdr *tcphdr = (struct dpdk_tcp_hdr *)(ip4hdr + 1);
    if (m->nb_segs == 1) {
        tcphdr->cksum = rte_ipv4_udptcp_cksum(ip4hdr, tcphdr);
    } else {
        uint16_t l4_off = (intptr_t)ip4hdr - (intptr_t)DPDK_HEADROOM(m)->l2;
        tcphdr->cksum = rte_ipv4_udptcp_cksum_mbuf(m, ip4hdr, l4_off);
    }
}

static INLINE bool dpdk_tcp4_mbuf_cksum_verify(const struct dpdk_mbuf *m, const struct dpdk_tcp_hdr *tcp_hdr)
{
    const struct dpdk_ip4_hdr *ip4hdr = DPDK_HEADROOM(m)->l3;

    if (LIKELY(rte_pktmbuf_is_contiguous(m))) {
        return (rte_ipv4_udptcp_cksum_verify(ip4hdr, tcp_hdr) == 0);
    } else {
        return (rte_ipv4_udptcp_cksum_mbuf_verify(m, ip4hdr, m->l2_len + m->l3_len) == 0);
    }
}

#endif // __DPDK_TCP_H__