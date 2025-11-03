/*****************************************************************************
 * filename: conf.h
 * function:
 * description: control common interface
 ****************************************************************************/

#pragma once

#include "data.h"
#include "dpdk_ip6.h"

#define CONF_NAME_LEN_MAX DATA_NAME_LEN_MAX

union inet_addr {
    unsigned int ip;
    unsigned int all[4];
    unsigned int ip6[4];
    unsigned char u8[16];
    struct dpdk_ip6_addr addr;
};

extern const char *conf_ip4_to_str(uint32_t);
extern const char *conf_ip6_to_str(const struct dpdk_ip6_addr *);
extern const char *conf_ip_to_str(int, const union inet_addr *);