/************************************************
 * filename: type.h
 * function:
 * description:
 ***********************************************/

#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>

#include "macro.h"

struct numa_config {
    void *arp;
    // TODO other module
} ALIGNED(CACHE_LINE);

struct thread_content {
    struct numa_config *nc; // Pointer to the configuration for this NUMA node

    uint8_t numa_idx; // NUMA index in your application (0-based)
    uint8_t local_idx; // Thread index within the NUMA node (0-based)

    uint16_t cpu_lcore; // Logical CPU (lcore) index used by the thread (0-based)
    uint16_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
    uint16_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)
} ALIGNED(CACHE_LINE);

struct numa_content {
    struct numa_config nc;

    struct {
        int tc_nums;
        struct thread_content *tc[];
    };
};

struct root {
    struct {
        int lock_fd;
        const char *lock_filename;
    };

    struct hw_info {
        int numa_nums;
        int cpu_nums;
        int nic_nums;
        uint64_t total_size;
        uint64_t hugepage_size;
    } hw_info;

    struct numa {
        int nums;
        struct numa_content numas[];
    } numa;
};

#endif // __TYPE_H__