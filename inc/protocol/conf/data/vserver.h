/*****************************************************************************
 * filename: vserver.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __VSERVER_H__
#define __VSERVER_H__

#include <stdint.h>

#include "data.h"
#include "protocol.h"
#include "dpdk_ip6.h"

#define VSERVER_ID_INVALID (UINT32_MAX)

enum VSERVER_STATUS {
    VSERVER_ONLINE,
    VSERVER_OFFLINE,
};

struct vserver_stats {
    uint32_t refcnt;
};

struct vserver_mutable {
    enum VSERVER_STATUS status;
};

struct vserver_base {
    uint32_t pool_id;
    uint32_t snat_pool_id;
    struct vserver_stats *stats;
    struct vserver_mutable *mutable;
};

struct vserver {
    int af;
    uint32_t id;
};

struct vserver_v4 {
    struct vserver vs;
    enum PROTO_TYPE type;
    uint16_t port;
    uint32_t vip;
    struct vserver_base base;

    char name[DATA_NAME_LEN_MAX];
};

struct vserver_v6 {
    struct vserver vs;
    enum PROTO_TYPE type;
    uint16_t port;
    struct dpdk_ip6_addr vip;
    struct vserver_base base;

    char name[DATA_NAME_LEN_MAX];
};

struct vserver4_key {
    uint32_t addr;
    uint16_t port;
    uint8_t protocol;
};

struct vserver6_key {
    struct dpdk_ip6_addr addr;
    uint16_t port;
    uint8_t protocol;
};

struct vserver4_kv_blk {
    int count;
    uint64_t result;
    struct vserver4_key v4_keys[DP_BATCH_MAX];
    struct vserver4_key *keys[DP_BATCH_MAX];
    struct vserver_v4 *data[DP_BATCH_MAX];
    void **mbufs;
};

struct vserver6_kv_blk {
    int count;
    uint64_t result;
    struct vserver6_key v6_keys[DP_MBUF_MAX];
    struct vserver6_key *keys[DP_BATCH_MAX];
    struct vserver_v6 *data[DP_BATCH_MAX];
    void **mbufs;
};

extern void *vserver_thread_create(void ***, int);
extern void vserver_thread_destroy(void *);
extern void vserver4_lookup(struct vserver4_kv_blk *);

#endif // __VSERVER_H__