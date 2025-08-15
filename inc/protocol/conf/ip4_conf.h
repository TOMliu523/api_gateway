/*****************************************************************************
 * filename: ip4_conf.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __IP4_CONF_H__
#define __IP4_CONF_H__

#include "l3.h"

extern int ip4_conf_manage_create_and_append(void **dst,
                                             void *src,
                                             const struct ip4_info *info,
                                             int count,
                                             int hw_numa_id);
extern int ip4_conf_manage_create_and_delete(void **dst,
                                             void *src,
                                             const struct ip4_info *info,
                                             int count,
                                             int hw_numa_id);
extern bool ip4_conf_manage_ip_is_local(const void *arg, uint32_t ip, uint8_t port);

#endif // __IP4_CONF_H__