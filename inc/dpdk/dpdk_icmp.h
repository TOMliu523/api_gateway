/*****************************************************************************
 * filename: dpdk_icmp.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_ICMP_H__
#define __DPDK_ICMP_H__

#include <stdbool.h>

#include <rte_cksum.h>

#include "util.h"
#include "dpdk_ip.h"
#include "dpdk_type.h"
#include "dpdk_common.h"

#define DPDK_ICMP_TYPE_ECHO_REPLY RTE_ICMP_TYPE_ECHO_REPLY
#define DPDK_ICMP_TYPE_ECHO_REQUEST RTE_ICMP_TYPE_ECHO_REQUEST
#define DPDK_ICMP_CODE_ECHO_REQUEST RTE_ICMP_CODE_UNREACH_NET

#define dpdk_icmp rte_icmp_hdr

static INLINE struct dpdk_icmp *dpdk_pktmbuf_icmp(struct dpdk_mbuf *m, uint16_t off)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_icmp *, off);
}

static INLINE bool dpdk_icmp_cksum_verify(const struct dpdk_mbuf *mbuf, const struct dpdk_icmp *icmp, uint16_t icmp_len)
{
    uint16_t cksum = 0;

    if (mbuf->nb_segs == 1) {
        cksum = rte_raw_cksum((const void *)icmp, icmp_len);
    } else {
        rte_raw_cksum_mbuf(mbuf, mbuf->pkt_len - icmp_len, icmp_len, &cksum);
    }

    return (cksum == UINT16_MAX) ? true : false;
}

static INLINE void dpdk_icmp_echo_reply_cksum(struct dpdk_icmp *icmp)
{
    uint16_t old_cksum = dpdk_be_to_cpu_16(icmp->icmp_cksum);
    uint16_t old_word = dpdk_be_to_cpu_16(*(uint16_t *)&icmp->icmp_type);

    icmp->icmp_type = DPDK_ICMP_TYPE_ECHO_REPLY;
    uint16_t new_word = dpdk_be_to_cpu_16(*(uint16_t *)&icmp->icmp_type);
    uint16_t cksum = util_cksum_incr_update(old_cksum, old_word, new_word);
    icmp->icmp_cksum = dpdk_cpu_to_be_16(cksum);
}

// TODO need debug
static INLINE void dpdk_icmp_cksum(const struct dpdk_mbuf *mbuf, struct dpdk_icmp *icmp, uint16_t icmp_len)
{
    uint16_t cksum = 0;

    icmp->icmp_cksum = 0;

    if (mbuf->nb_segs == 1) {
        cksum = rte_raw_cksum((const void *)icmp, icmp_len);
        icmp->icmp_cksum = dpdk_cpu_to_be_16(cksum);
    } else {
        rte_raw_cksum_mbuf(mbuf, mbuf->pkt_len - icmp_len, icmp_len, &cksum);
        icmp->icmp_cksum = dpdk_cpu_to_be_16(cksum);
    }
}

#endif // __DPDK_ICMP_H__