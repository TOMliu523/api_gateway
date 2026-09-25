/************************************************
 * filename: dpdk_type.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_TYPE_H__
#define __DPDK_TYPE_H__

#include <stdint.h>

#include <rte_ip.h>
#include <rte_arp.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_icmp.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>

#include "macro.h"

#ifndef DPDK_DATA_LEN_MAX
#define DPDK_DATA_LEN_MAX RTE_MBUF_DEFAULT_BUF_SIZE
#endif // DPDK_DATA_LEN_MAX

#define DPDK_PTYPE_ETHER RTE_PTYPE_L2_ETHER
#define DPDK_PTYPE_ARP RTE_PTYPE_L2_ETHER_ARP
#define DPDK_PTYPE_LLDP RTE_PTYPE_L2_ETHER_LLDP
#define DPDK_PTYPE_VLAN RTE_PTYPE_L2_ETHER_VLAN
#define DPDK_PTYPE_L2_TYPE(type) ((type) & RTE_PTYPE_L2_MASK)

#define DPDK_PTYPE_IPV4 RTE_PTYPE_L3_IPV4
#define DPDK_PTYPE_IPV6 RTE_PTYPE_L3_IPV6
#define DPDK_PTYPE_L3_TYPE(type) ((type) & RTE_PTYPE_L3_MASK)

#define DPDK_PTYPE_TCP RTE_PTYPE_L4_TCP
#define DPDK_PTYPE_UDP RTE_PTYPE_L4_UDP
#define DPDK_PTYPE_ICMP RTE_PTYPE_L4_ICMP
#define DPDK_PTYPE_IGMP RTE_PTYPE_L4_IGMP
#define DPDK_PTYPE_L4_TYPE(type) ((type) & RTE_PTYPE_L4_MASK)

#define dpdk_mbuf rte_mbuf
#define dpdk_ring rte_ring
#define dpdk_pool rte_mempool
#define dpdk_mac rte_ether_addr
#define dpdk_eth rte_ether_hdr
#define dpdk_arp rte_arp_hdr
#define dpdk_ipv4 rte_ipv4_hdr
#define dpdk_ipv6 rte_ipv6_hdr
#define dpdk_icmp rte_icmp_hdr
#define dpdk_tcp rte_tcp_hdr
#define dpdk_udp rte_udp_hdr

// Ethernet frame types
#define DPDK_ETHER_IPV4 RTE_ETHER_TYPE_IPV4
#define DPDK_ETHER_IPV6 RTE_ETHER_TYPE_IPV6
#define DPDK_ETHER_ARP RTE_ETHER_TYPE_ARP
#define DPDK_ETHER_RARP RTE_ETHER_TYPE_RARP
#define DPDK_ETHER_VLAN RTE_ETHER_TYPE_VLAN
#define DPDK_ETHER_LLDP RTE_ETHER_TYPE_LLDP

// Byte order conversion
#define dpdk_to_be_16(v) rte_cpu_to_be_16(v)
#define dpdk_to_be_32(v) rte_cpu_to_be_32(v)
#define dpdk_to_be_64(v) rte_cpu_to_be_64(v)

// type
struct dpdk_icmp6 {
    uint8_t icmp6_type;
    uint8_t icmp6_code;
    uint16_t icmp6_chsum;

    union {
        uint32_t un_data32[1];
        uint32_t un_data16[2];
        uint8_t un_data8[4];

        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
    } icmp6_data;
};

struct dpdk_headroom {
    union {
        struct {

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