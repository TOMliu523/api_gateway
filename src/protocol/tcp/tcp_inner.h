/*****************************************************************************
 * filename: tcp_inner.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __TCP_INNER_H__
#define __TCP_INNER_H__

#include "tcp.h"
#include "type.h"
#include "dpdk_tcp.h"
#include "dpdk_type.h"

struct tcp_ops {
    void (*rst_reply)(struct dpdk_mbuf *, uint32_t seq, uint32_t ack);
    int (*syn_ack_reply)(struct dpdk_mbuf *, struct tcp_conn *);
};

static INLINE bool tcp4_mbuf_cksum_verify(const struct dpdk_mbuf *mbuf, const struct dpdk_tcp_hdr *tcphdr)
{
    switch (mbuf->ol_flags & DPDK_TCP_RX_CKSUM_MASK) {
    case DPDK_TCP_RX_CKSUM_GOOD: return true;
    case DPDK_TCP_RX_CKSUM_BAD: return false;
    default: return dpdk_tcp4_mbuf_cksum_verify(mbuf, tcphdr);
    }

    return true;
}

#endif // __TCP_INNER_H__