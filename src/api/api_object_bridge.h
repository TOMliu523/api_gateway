/*****************************************************************************
 * filename: api_object_bridge.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __API_OBJECT_BRIDGE_H__
#define __API_OBJECT_BRIDGE_H__

#include <stdint.h>

#include "ip4.h"

extern int ip4_info_init(struct ip4_info *, uint32_t, uint8_t, uint16_t, enum IP_TYPE, uint32_t);
extern int ip6_info_init(struct ip6_info *, const struct dpdk_ip6_addr *, uint8_t, uint16_t, enum IP_TYPE, uint32_t);

extern void snat_conf_destroy(void *);

#endif // __API_OBJECT_BRIDGE_H__