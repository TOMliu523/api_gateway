/*****************************************************************************
 * filename: l3.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __L3_H__
#define __L3_H__

#include <stdbool.h>

#include "list.h"
#include "dpdk_type.h"

#define L3_IPv4_BUCKET_MAX (1 << 16)
#define L3_IPv4_BUCKET_IDX(x) ((x) >> 16)

enum IP_TYPE {
    IP_MASTER,
    IP_SECONDARY,
};

struct ipv4_info {
    struct list_head node;
    uint32_t ip;
    uint8_t mask;
    uint16_t port;
    enum IP_TYPE type;
};

struct ipv6_info {
    struct list_head node;
    union dpdk_ipv6_addr ipv6;
    uint8_t mask;
    uint16_t port;
    uint32_t hash;
    enum IP_TYPE type;
};

struct ipv4_manage {
    int ip_count;
    struct ipv4_info **info;
    struct list_head head[L3_IPv4_BUCKET_MAX];
};

extern void l3_do(void *);
extern void l3_refresh(void);
extern void *l3_thread_ipv4_create(int);
extern void l3_thread_ipv4_destroy(void *);

extern bool l3_is_our_ipv4(uint16_t port, uint32_t ip);

#endif // __L3_H__