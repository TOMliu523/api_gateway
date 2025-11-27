/*****************************************************************************
 * filename: ip4.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

#include <stdint.h>

#include "l3.h"
#include "dpdk_ip4.h"

#define IP4_HDR_MIN (sizeof(struct dpdk_ip4_hdr))

extern void ip4_process(void *[], int);
extern void ip4_table_destroy(void *);
extern void *ip4_table_startup(void ***, int, int);

extern void ip4_arp_refresh(void);

/**
 * @brief Lightweight dataplane API to classify a local IPv4 address.
 *
 * This version is optimized for high-speed dataplane usage, avoiding
 * any locking or logging overhead. It should be called only from
 * data path threads where state is already up-to-date and safe.
 *
 * @param port The port ID (physical or logical interface).
 * @param ip   The IPv4 address in network byte order.
 * @return One of the IP_LOCAL_CLASS enum values:
 *         - IP_LOCAL_CLASS_OTHER: Not local and not in same subnet.
 *         - IP_LOCAL_CLASS_SELF: The IP is assigned to the local interface.
 *         - IP_LOCAL_CLASS_LAN: The IP is within the same subnet but not local.
 */
extern enum IP_LOCAL_CLASS ip4_local_class(uint16_t port, uint32_t ip);

extern void ip4_header_init(struct dpdk_ip4_hdr *ip4hdr, uint32_t saddr,
                            uint32_t daddr, uint8_t proto, uint16_t payload_len);
extern void ip4_pktmbuf_replay(struct dpdk_mbuf *m, uint8_t proto, uint16_t total_len);