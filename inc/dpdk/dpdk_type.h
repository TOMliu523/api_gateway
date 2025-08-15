/************************************************
 * filename: dpdk_type.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_TYPE_H__
#define __DPDK_TYPE_H__

#include <stdint.h>

#include <rte_arp.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_icmp.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>

#include "macro.h"
#include "dpdk_limits.h"

#ifndef DPDK_DATA_LEN_MAX
#define DPDK_DATA_LEN_MAX RTE_MBUF_DEFAULT_BUF_SIZE
#endif // DPDK_DATA_LEN_MAX

#define DPDK_PTYPE_ETHER RTE_PTYPE_L2_ETHER
#define DPDK_PTYPE_ARP RTE_PTYPE_L2_ETHER_ARP
#define DPDK_PTYPE_LLDP RTE_PTYPE_L2_ETHER_LLDP
#define DPDK_PTYPE_VLAN RTE_PTYPE_L2_ETHER_VLAN
#define DPDK_PTYPE_L2_TYPE(type) ((type) & RTE_PTYPE_L2_MASK)

#define DPDK_PTYPE_TCP RTE_PTYPE_L4_TCP
#define DPDK_PTYPE_UDP RTE_PTYPE_L4_UDP
#define DPDK_PTYPE_ICMP RTE_PTYPE_L4_ICMP
#define DPDK_PTYPE_IGMP RTE_PTYPE_L4_IGMP
#define DPDK_PTYPE_L4_TYPE(type) ((type) & RTE_PTYPE_L4_MASK)

#define dpdk_mbuf rte_mbuf
#define dpdk_mac rte_ether_addr
#define dpdk_eth rte_ether_hdr
#define dpdk_arp rte_arp_hdr
#define dpdk_tcp rte_tcp_hdr
#define dpdk_udp rte_udp_hdr
#define dpdk_arp_data rte_arp_ipv4

// Ethernet frame types
#define DPDK_ETHER_IP4 RTE_ETHER_TYPE_IPV4
#define DPDK_ETHER_IP6 RTE_ETHER_TYPE_IPV6
#define DPDK_ETHER_ARP RTE_ETHER_TYPE_ARP
#define DPDK_ETHER_RARP RTE_ETHER_TYPE_RARP
#define DPDK_ETHER_VLAN RTE_ETHER_TYPE_VLAN
#define DPDK_ETHER_LLDP RTE_ETHER_TYPE_LLDP

#define DPDK_HEADROOM(m) (&((struct dpdk_data *)(m))->headroom)

enum PKT_MBUF_TYPE {
    PKT_MBUF_DEFAULT = 0, // data packet
    PKT_MBUF_GARP,
    PKT_MBUF_ARP,
    PKT_MBUF_NDP,
};

struct dpdk_headroom {
    union { // PKT_MBUF_DEFAULT
        // host byte order
        struct {
            enum PKT_MBUF_TYPE type;
            uint8_t l2_type;
            uint8_t l3_type;
            uint8_t l4_type;

            void *l2;
            void *l3;
            void *l4;
        };
        uint8_t reserve[2 * CACHE_LINE];
    };
} ALIGNED(CACHE_LINE);

struct dpdk_data {
    struct dpdk_mbuf mbuf;
    struct dpdk_headroom headroom;
    uint8_t packet[]; // debug
};

#endif // __DPDK_TYPE_H__