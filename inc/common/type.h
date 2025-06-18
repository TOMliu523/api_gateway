/************************************************
 * filename: type.h
 * function:
 * description:
 ***********************************************/

#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>

#include "macro.h"

#define NUMA_MAX 32
#define NUMA_CPU_MAX 64

struct numa_config {
    void *arp;
    // ... other per-NUMA modules
} ALIGNED(CACHE_LINE);

struct numa_content {
    struct numa_config nc;

    struct {
        int nums;
        void *dpdk_thread[NUMA_CPU_MAX];
    };
};

struct hw_info {
    int numa_count;
    int cpu_count;
    int nic_count;
    uint64_t total_memory;
    uint64_t hugepage_size;
};

struct numa_node {
    int nums;
    struct numa_content contents[];
};

struct root {
    struct {
        int lock_fd;
        const char *lock_filename;
    };

    struct hw_info hw_info;
    // must last elements
    struct numa_node numa;
};

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

#endif // __TYPE_H__