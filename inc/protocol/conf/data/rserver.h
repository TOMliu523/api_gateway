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

#define RSERVER_INVALID_ID (-1)

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
};

struct rserver_base {
    uint16_t port;
    struct rserver_stat *stat;
    struct rserver_mutable *mtb;
};

struct rserver_v4 {
    struct rserver rs;
    uint32_t addr;
    struct rserver_base base;
};

struct rserver_v6 {
    struct rserver rs;
    struct dpdk_ip6_addr addr;
    struct rserver_base base;
};

extern void *rserver_thread_create(int);
extern void rserver_thread_destroy(void *);

static INLINE void rserver_v4_refcnt_inc(struct rserver_v4 *v4)
{
    v4->base.stat->refcnt += 1;
}

static INLINE void rerver_v4_refcnt_dec(struct rserver_v4 *v4)
{
    v4->base.stat->refcnt -= 1;
}

static INLINE void rserver_v6_refcnt_inc(struct rserver_v6 *v6)
{
    v6->base.stat->refcnt += 1;
}

static INLINE void rserver_v6_refcnt_dec(struct rserver_v6 *v6)
{
    v6->base.stat->refcnt -= 1;
}