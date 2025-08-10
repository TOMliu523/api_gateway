/*****************************************************************************
 * filename: dpdk_icmp6.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_ICMP6_H__
#define __DPDK_ICMP6_H__

#include "dpdk_ip6.h"

#define DPDK_DNP_SOLICITATION 135
#define DPDK_DNP_ADVERTISEMENT 136

#define DPDK_LOCAL_HOP_LIMITS 255

struct dpdk_ndp_opt {
    __u8 type;
    __u8 len;
    __u8 data[];
} ALIGN_PACKED;

struct dpdk_ndp {
    struct icmp6hdr icmp6_hdr;
    union dpdk_ip6_addr target;
    __u8 opt[];
} ALIGN_PACKED;

/*
 * The IPv6 header must not be followed by extension headers. The layer 4
 * checksum must be set to 0 in the L4 header by the caller.
 */
static INLINE uint16_t dpdk_icmp6_cksum(const struct dpdk_mbuf *mbuf, const struct dpdk_ip6_hdr *ip6hdr, uint16_t l4_off)
{
    uint32_t sum;
    uint16_t raw_cksum;
    uint16_t icmp_len = dpdk_be_to_cpu_16(ip6hdr->payload_len);

    if (mbuf->nb_segs == 1) {
        raw_cksum = rte_raw_cksum(mbuf->buf_addr + l4_off, icmp_len);
    } else {
        rte_raw_cksum_mbuf(mbuf, l4_off, icmp_len, &raw_cksum);
    }

    sum = (uint32_t)raw_cksum + rte_ipv6_phdr_cksum(ip6hdr, 0);
    sum = (sum & 0xFFFF) + (sum >> 16);
    sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t cksum = (uint16_t) ~sum;

    return cksum;
}

#endif // __DPDK_ICMP6_H__