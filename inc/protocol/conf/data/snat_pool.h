/*****************************************************************************
 * filename: snat_pool.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __SNAT_POOL_H__
#define __SNAT_POOL_H__

#include <stdint.h>

#include "conf.h"
#include "list.h"

enum SNAT_ADDR_POLICY {
    SNAT_ADDR_INVALID = -1,
    SNAT_ADDR_IP_RR = 0,
    SNAT_ADDR_CONN_RR,
    SNAT_ADDR_LEAST_CONN,
    SNAT_ADDR_PRESERVE_PORT,
    SNAT_ADDR_MAX,
};

struct snat_ip4_rr {
    uint32_t count;
    uint32_t next; // next id
    uint32_t *addr;
    uint16_t min_port;
    uint16_t max_port;
};

struct snat_ip6_rr {
    uint32_t count;
    uint32_t next;
    struct dpdk_ip6_addr *addr;
    uint16_t min_port;
    uint16_t max_port;
};

struct snat_pool {
    uint32_t id;
    uint8_t port;
    int (*snat_ip4_get_next)(uint32_t *, void *);
    int (*snat_ip6_get_next)(struct dpdk_ip6_addr *, void *);
    union {
        struct {
            struct snat_ip4_rr *ip4_rr;
            struct snat_ip6_rr *ip6_rr;
        };

        void *ptr;
    };

    struct {
        uint32_t refcnt;
        enum SNAT_ADDR_POLICY policy;
        char name[CONF_NAME_LEN_MAX];
    };
};

extern void snat_thread_config_refresh(void *);
extern void *snat_thread_create(void ***, int);
extern void snat_thread_destroy(void *);

#endif // __SNAT_POOL_H__