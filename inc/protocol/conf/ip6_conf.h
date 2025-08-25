/*****************************************************************************
 * filename: ip6_conf.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __IP6_CONF_H__
#define __IP6_CONF_H__

#include "l3.h"

extern int ip6_conf_manage_create_and_append(void **dst,
                                             void *src,
                                             const struct ip6_info *info,
                                             int count,
                                             int hw_numa_id);
extern int ip6_conf_manage_create_and_delete(void **dst,
                                             void *src,
                                             const struct ip6_info *info,
                                             int count,
                                             int hw_numa_id);
extern bool ip6_conf_manage_ip_is_local(const void *arg, const struct dpdk_ip6_addr *addr, uint8_t port);

extern void ip6_conf_manage_destroy(void *);

extern int ip6_ndp_unsolicited_na_gen(struct dpdk_mbuf *, uint16_t port, const struct dpdk_ip6_addr *, const struct dpdk_mac *);

#endif // __IP6_CONF_H__