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

struct root {
    struct {
        int lock_fd;
        const char *lock_filename;
    };

    struct hw_info {
        int numa_count;
        int cpu_count;
        int nic_count;
        uint64_t total_memory;
        uint64_t hugepage_size;
    } hw_info;

    struct numa_node {
        int nums;
        struct numa_content contents[];
    } numa;
};

#endif // __TYPE_H__