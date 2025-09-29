/*****************************************************************************
 * filename: rserver.h
 * function:
 * description:
 ****************************************************************************/

#pragma once

#include <stdint.h>

#include "list.h"
#include "dpdk_ip4.h"
#include "dpdk_ip6.h"
#include "dpdk_atomic.h"

#define RS_INVALID_ID (-1)

enum RSERVER_STATUS {
    RSERVER_ONLINE = 0,
    RSERVER_OFFLINE,
};

struct rserver_stat {
    uint32_t refcnt;
};

/*
 * This structure defines the mutable configuration of real servers.
 * These fields can only be updated by the control plane; in the data plane
 * they are strictly read-only and must not be modified.
 */
struct rserver_mutable {
    uint32_t pool_refcnt;
    enum RSERVER_STATUS status;
};

struct rserver {
    int af;
    int id;
    struct list_head node;
};

struct rserver_base {
    uint16_t port;
    struct rserver_stat *stat;
    struct rserver_mutable *mtb;
};

struct rserver_v4 {
    struct rserver rs;
    uint32_t ip;
    struct rserver_base base;
};

struct rserver_v6 {
    struct rserver rs;
    struct dpdk_ip6_addr ip6;
    struct rserver_base base;
};

extern int rserver_init(int);
extern void rserver_fini(void);