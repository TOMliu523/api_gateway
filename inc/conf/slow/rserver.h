/*****************************************************************************
 * filename: rserver.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __RSERVER_H__
#define __RSERVER_H__

#include <stdint.h>

#include "dpdk_ip4.h"
#include "dpdk_ip6.h"
#include "dpdk_atomic.h"

struct rserver_mutable {
    dpdk_atomic32_t refcnt;
};

struct rserver {
    int af;
};

struct rserver_v4 {
    struct rserver rs;
    uint32_t ip;
    struct rserver_mutable *mtb;
};

struct rserver_v6 {
    struct rserver rs;
    struct dpdk_ip6_addr ip6;
    struct rserver_mutable *mtb;
};

#endif // __RSERVER_H__