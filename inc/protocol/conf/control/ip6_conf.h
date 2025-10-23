/*****************************************************************************
 * filename: ip6.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

#include "l3.h"

extern int ip6_conf_table_create_and_append(void **dst,
                                             void *src,
                                             const struct ip6_info *info,
                                             int count,
                                             int hw_numa_id);
extern int ip6_conf_table_create_and_delete(void **dst,
                                             void *src,
                                             const struct ip6_info *info,
                                             int count,
                                             int hw_numa_id);
extern bool ip6_conf_table_ip_is_local(const void *arg, const struct dpdk_ip6_addr *addr, uint8_t port);

extern void ip6_conf_table_destroy(void *);

extern int ip6_ndp_na_mcast_gen(struct dpdk_mbuf *, uint16_t port, const struct dpdk_ip6_addr *, const struct dpdk_mac *);
extern int ip6_info_init(struct ip6_info *, const struct dpdk_ip6_addr *, uint8_t,
                         uint16_t, enum IP_TYPE, uint32_t);