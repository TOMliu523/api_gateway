/*****************************************************************************
 * filename: vserver.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __VSERVER_H__
#define __VSERVER_H__

#include "conf.h"

enum VSERVER_TYPE {
    VSERVER_TCP,
    VSERVER_UDP,
    VSERVER_HTTP,
    VSERVER_HTTPS,
    VSERVER_HTTP2,
    VSERVER_HTTP3,
    VSERVER_MAX,
};

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
};

struct vserver_v4 {
    struct vserver vs;
    enum VSERVER_TYPE type;
    uint16_t port;
    uint32_t vip;
    struct vserver_base base;

    char name[CONF_NAME_LEN_MAX];
};

struct vserver_v6 {
    struct vserver vs;
    enum VSERVER_TYPE type;
    uint16_t port;
    struct dpdk_ip6_addr vip;
    struct vserver_base base;

    char name[CONF_NAME_LEN_MAX];
};

extern int vserver_thread_create(int);
extern void vserver_thread_destroy(void *);
extern void vserver_thread_config_refresh(void *);

#endif // __VSERVER_H__