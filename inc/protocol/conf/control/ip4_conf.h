/*****************************************************************************
 * filename: ip4.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

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