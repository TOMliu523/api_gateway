/*****************************************************************************
 * filename: route.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __ROUTE_H__
#define __ROUTE_H__

#include <stdint.h>
#include <stdbool.h>

#include "list.h"
#include "dpdk_fib.h"

enum ROUTE_TYPE {
    ROUTE_DIRECT,
    ROUTE_MANUAL,
    ROUTE_PROTO_BGP,
    ROUTE_PROTO_OSPF,
    ROUTE_PROTO_ISIS,
    ROUTE_PROTO_RIP,
};

struct route_item {
    struct list_head lru_head;   // Aging list entry
	uint32_t nexthop;            // Next hop IP address (network byte order)
	uint32_t dst_subnet;             // Destination IP address (network byte order)
	uint8_t mask;                // Subnet mask length (e.g., 24 for /24)
	uint8_t route_type;               // Route protocol type (see ROUTE_TYPE enum)
	uint8_t priority;            // Route priority (lower value means higher priority)
	uint8_t interface;           // Egress interface ID
	uint32_t last_access_time;   // Timestamp of last access (used for aging/LRU)
	uint32_t last_probe_time;    // Timestamp of last probe or activity (e.g., ARP)
	uint8_t valid;               // Route validity flag (1 = valid, 0 = invalid)
	uint16_t direct_id;          // ID of associated direct route (used for recursive lookup)
};

extern int route_conf_create_and_append(void **dst,
                                        void *src,
                                        const struct route_item *item,
                                        int count,
                                        int hw_numa_id,
                                        const void *arg);
extern int route_conf_create_and_delete(void **dst,
                                        void *src,
                                        const struct route_item *items,
                                        int count,
                                        int hw_numa_id,
                                        bool is_route);
extern void route_conf_table_get(void *src, struct route_item **item, int *count);
extern void route_conf_destroy(void *ptr);
extern void route_conf_update_lock(void);
extern void route_conf_update_unlock(void);

#endif // __ROUTE_H__