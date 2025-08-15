/*****************************************************************************
 * filename: route4_conf.h
 * function:
 * description:
 ****************************************************************************/

 #ifndef __ROUTE4_CONF_H__
 #define __ROUTE4_CONF_H__

 #include <stdint.h>
 #include <stdbool.h>

 #include "list.h"
 #include "dpdk_fib.h"

 enum ROUTE4_TYPE {
     ROUTE4_DIRECT,
     ROUTE4_MANUAL,
     ROUTE4_PROTO_BGP,
     ROUTE4_PROTO_OSPF,
     ROUTE4_PROTO_ISIS,
     ROUTE4_PROTO_RIP,
 };

 struct route4_item {
     struct list_head lru_head;   // Aging list entry
     uint32_t nexthop;            // Next hop IP address (network byte order)
     uint32_t dst_subnet;         // Destination IP address (network byte order)
     uint8_t mask;                // Subnet mask length (e.g., 24 for /24)
     uint8_t route_type;          // Route protocol type (see ROUTE_TYPE enum)
     uint8_t priority;            // Route priority (lower value means higher priority)
     uint8_t interface;           // Egress interface ID
     uint32_t last_access_time;   // Timestamp of last access (used for aging/LRU)
     uint32_t last_probe_time;    // Timestamp of last probe or activity (e.g., ARP)
     uint8_t valid;               // Route validity flag (1 = valid, 0 = invalid)
     uint16_t direct_id;          // Reference to associated direct route for recursive resolution
 };

 extern int route4_conf_create_and_append(void **dst,
                                         void *src,
                                         const struct route4_item *item,
                                         int count,
                                         int hw_numa_id,
                                         const void *arg);
 extern int route4_conf_create_and_delete(void **dst,
                                         void *src,
                                         const struct route4_item *items,
                                         int count,
                                         int hw_numa_id,
                                         bool is_route);
 extern void route4_conf_table_get(void *src, struct route4_item **item, int *count);
 extern void route4_conf_destroy(void *ptr);
 extern void route4_conf_update_lock(void);
 extern void route4_conf_update_unlock(void);

 #endif // __ROUTE4_CONF_H__