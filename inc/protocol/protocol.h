/*****************************************************************************
 * filename: protocol.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include "l2.h"
#include "route4.h"
#include "route6.h"

struct proto_header {
    struct dpdk_mac *mac;
    struct {
        struct arp_table *at;
        struct route4_table *route4;
    };

    struct {
        struct route6_table *route6;
    };
};

extern void *protocol_create(int ,void *);
extern void protocol_destroy(void *);

#endif // __PROTOCOL_H__