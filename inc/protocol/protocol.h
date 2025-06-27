/*****************************************************************************
 * filename: protocol.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

struct proto_header {
    void *mac;
    void *at;
    void *ipv4;
    void *ipv6;
    // struct neigh_table *nt;
};

extern void *protocol_create(int ,void *);
extern void protocol_destroy(void *);

#endif // __PROTOCOL_H__