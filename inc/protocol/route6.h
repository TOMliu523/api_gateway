
/*****************************************************************************
 * filename: route6.h
 * function:
 * description: High-Performance Longest Prefix Match Library
 ****************************************************************************/

#ifndef __ROUTE6_H__
#define __ROUTE6_H__

#include "list.h"
#include "dpdk_type.h"

enum ROUTE6_TYPE {
    ROUTE6_DIRECT,
    ROUTE6_MANUAL,
    ROUTE6_PROTO_BGP,
    ROUTE6_PROTO_OSPF,
    ROUTE6_PROTO_ISIS,
    ROUTE6_PROTO_RIP,
};

struct route6_item {
    struct list_head lru_head;          // Linked list node for aging/LRU tracking
    union dpdk_ipv6_addr nexthop;       // Next hop IPv6 address
    union dpdk_ipv6_addr dst_subnet;    // Destination IPv6 subnet address
    uint8_t mask;                       // Subnet mask length (e.g., 64 for /64)
    uint8_t route_type;                 // Routing protocol type (see ROUTE6_TYPE enum)
    uint8_t priority;                   // Route priority (lower value = higher priority)
    uint8_t interface;                  // Egress interface ID or index
    uint32_t last_access_time;          // Timestamp of last packet match (for aging/LRU)
    uint32_t last_probe_time;           // Timestamp of last neighbor resolution (e.g., ND probe)
    uint8_t valid;                      // Route validity flag (1 = valid, 0 = invalid)
    uint16_t direct_id;                 // Reference to associated direct route for recursive resolution
};

extern int route6_conf_create_and_append(void **dst,
                                         void *src,
                                         const struct route6_item *item,
                                         int count,
                                         int hw_numa_id,
                                         const void *arg);
extern int route6_conf_create_and_delete(void **dst,
                                         void *src,
                                         const struct route6_item *item,
                                         int count,
                                         int hw_numa_id,
                                         bool is_route);
extern void route6_conf_table_get(void *src, struct route6_item **item, int *count);
extern void route6_conf_destroy(void *ptr);

#endif // __ROUTE6_H__