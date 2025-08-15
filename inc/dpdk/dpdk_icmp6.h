/*****************************************************************************
 * filename: dpdk_icmp6.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_ICMP6_H__
#define __DPDK_ICMP6_H__

#include <stdbool.h>
#include <linux/icmpv6.h>

#include "util.h"
#include "dpdk_l4.h"
#include "dpdk_ip6.h"

#define DPDK_DNP_SOLICITATION 135
#define DPDK_DNP_ADVERTISEMENT 136

#define DPDK_LOCAL_HOP_LIMITS 255

#define DPDK_ICMP6_ECHO_REQUEST ICMPV6_ECHO_REQUEST
#define DPDK_ICMP6_ECHO_REPLY ICMPV6_ECHO_REPLY
#define DPDK_NDP_SOLICT 135
#define DPDK_NDP_ADVERT 136

#define dpdk_icmp6_hdr icmp6hdr

struct dpdk_ndp_opt {
    __u8 type;
    __u8 len;
    __u8 data[];
} ALIGN_PACKED;

struct dpdk_ndp {
    struct dpdk_icmp6_hdr icmp6_hdr;
    struct dpdk_ip6_addr target;
    __u8 opt[];
} ALIGN_PACKED;

/**
 * Verify the ICMPv6 checksum of a received IPv6 packet.
 *
 * This function first checks if the NIC has already validated the L4 checksum
 * via Rx offload flags. If so, it returns immediately based on the offload
 * result. Otherwise, it calculates the ICMPv6 checksum in software.
 *
 * The software calculation uses DPDK's internal IPv6 UDP/TCP checksum helpers,
 * which also work for ICMPv6 since they compute the IPv6 pseudo-header and
 * payload checksum. For single-segment packets, the direct memory version is
 * used; for multi-segment packets, the mbuf-walk version is used.
 *
 * Limitations:
 * - Assumes ICMPv6 header starts immediately after the IPv6 header
 *   (i.e., no IPv6 extension headers are present).
 * - Caller must ensure `ip6hdr` points to the IPv6 header within the mbuf data.
 *
 * @param mbuf
 *   The packet mbuf containing the IPv6 frame.
 * @param ip6hdr
 *   Pointer to the IPv6 header in the packet.
 *
 * @return
 *   true  - checksum valid
 *   false - checksum invalid
 */
static INLINE bool dpdk_icmp6_cksum_verify(const struct dpdk_mbuf *mbuf, const struct dpdk_ip6_hdr *ip6hdr)
{
    uint16_t cksum;

    if ((mbuf->ol_flags & DPDK_RX_L4_CKSUM_GOOD) != 0) return true;
    if ((mbuf->ol_flags & DPDK_RX_L4_CKSUM_BAD) != 0) return false;

    if (mbuf->nb_segs == 1) {
        cksum = __rte_ipv6_udptcp_cksum(ip6hdr, ip6hdr + 1);
    } else {
        const uint16_t l3_off = (uint16_t)((const uint8_t *)ip6hdr - (const uint8_t *)rte_pktmbuf_mtod(mbuf, const uint8_t *));
        cksum = __rte_ipv6_udptcp_cksum_mbuf(mbuf, ip6hdr, l3_off + sizeof(*ip6hdr));
    }

    return (cksum == 0xFFFF) ? true : false;
}

static INLINE void dpdk_icmp6_echo_reply_ckcum(struct dpdk_icmp6_hdr *icmp6hdr)
{
    uint16_t old_cksum = dpdk_be_to_cpu_16(icmp6hdr->icmp6_cksum);
    uint16_t old_word = dpdk_be_to_cpu_16(*(uint16_t *)&icmp6hdr->icmp6_type);

    icmp6hdr->icmp6_type = DPDK_ICMP6_ECHO_REPLY;
    uint16_t new_word = dpdk_be_to_cpu_16(*(uint16_t *)&icmp6hdr->icmp6_type);
    uint16_t cksum = util_cksum_incr_update(old_cksum, old_word, new_word);
    icmp6hdr->icmp6_cksum = dpdk_cpu_to_be_16(cksum);
}

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