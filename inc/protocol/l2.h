/************************************************
 * filename: l2.h
 * function:
 * description:
 ***********************************************/

#ifndef __L2_H__
#define __L2_H__

#include <stdint.h>

struct mac {
    uint8_t bytes[6];
};

struct arp {
    uint16_t hrd;      // Hardware type (Ethernet = 1)
    uint16_t pro;      // Protocol type (IPv4 = 0x0800)
    uint8_t  hln;      // Hardware address length (Ethernet = 6)
    uint8_t  pln;      // Protocol address length (IPv4 = 4)
    uint16_t op;       // Operation code (1 = ARP Request, 2 = ARP Reply)
    struct mac sha;    // Sender hardware address (MAC)
    uint32_t spa;      // Sender protocol address (IPv4 address)
    struct mac tha;    // Target hardware address (MAC)
    uint32_t tpa;      // Target protocol address (IPv4 address)
} DPDK_PACKED;

#endif // __L2_H__