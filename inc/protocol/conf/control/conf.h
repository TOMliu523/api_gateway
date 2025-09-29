/*****************************************************************************
 * filename: conf.h
 * function:
 * description: control common interface
 ****************************************************************************/

#pragma once

#include "dpdk_ip6.h"

extern const char *conf_ip_to_str(uint32_t);
extern const char *conf_ip6_to_str(struct dpdk_ip6_addr *);