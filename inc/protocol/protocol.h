/*****************************************************************************
 * filename: protocol.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include "l2.h"
#include "l3.h"
#include "route.h"

struct proto_header {
    struct dpdk_mac *mac;
    struct {
        struct arp_table *at;
        struct route_table *route;
    };

    struct {
        // struct arp_v6_table *at_v6;
        // struct route_v6_table route_v6;
        // struct ipv6_manage *ipv6;
    };
};

extern void *protocol_create(int ,void *);
extern void protocol_destroy(void *);

#endif // __PROTOCOL_H__