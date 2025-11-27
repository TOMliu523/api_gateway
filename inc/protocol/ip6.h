/*****************************************************************************
 * filename: ip6.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

#include "l3.h"
#include "dpdk_type.h"

#define IP6_HDR_MIN (sizeof(struct dpdk_ip6_hdr))

extern void ip6_process(void **, int);
extern void ip6_table_destroy(void *);

extern void *ip6_table_startup(void ***, int, int);
extern void *ip6_thread_ndp_table_create(void ***, int, int, int);
extern void ip6_thread_ndp_table_destroy(void *);

extern void ip6_ndp_refresh(void);
extern void ip6_ndp_update_or_create(void *);

extern void ip6_header_init(struct dpdk_mbuf *m, struct dpdk_ip6_addr *saddr,
                            struct dpdk_ip6_addr *daddr, uint8_t proto, uint16_t len);

extern void ip6_pktmbuf_reply(struct dpdk_mbuf *m, uint8_t proto, uint16_t payload_len);