/************************************************
 * filename: type.h
 * function:
 * description:
 ***********************************************/

#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>

#include "macro.h"

#ifndef NUMA_MAX
#define NUMA_MAX 32
#endif // NUMA_MAX

#ifndef NUMA_PER_CPU_MAX
#define NUMA_PER_CPU_MAX 64
#endif // NUMA_PER_CPU_MAX

#ifndef CPU_MAX
#define CPU_MAX 256
#endif // CPU_MAX

struct iface {
    int nums;
    uint16_t port[];
};

struct numa_config {
    uint64_t version;
    void *arp;
    struct iface *iface;
    // ... other per-NUMA modules
} ALIGNED(CACHE_LINE);

struct stats {
    uint64_t rx_pkt_count;
} ALIGNED(CACHE_LINE);

struct dataplane {
    uint64_t version;
    struct numa_config *nc; // Pointer to the configuration for this NUMA node
    void *pktmbuf;
    void *notice_ring;
    uint8_t numa_id; // NUMA index in your application (0-based)
    uint8_t numa_cpu_id; // Thread index within the NUMA node (0-based)
    uint8_t cpu_id; // *Logical CPU (lcore) index used by the thread (0-based)
    uint8_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
    uint8_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)

    struct stats *stats;
} ALIGNED(CACHE_LINE);

struct hw_info {
    int numa_count;
    int cpu_count;
    int nic_count;
    uint64_t total_memory;
    uint64_t hugepage_size;
};

struct root {
    struct hw_info hw_info;
    struct dataplane *dpdk_thread[CPU_MAX];
};

#endif // __TYPE_H__