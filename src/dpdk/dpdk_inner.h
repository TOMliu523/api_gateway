/************************************************
 * filename: dpdk_inner.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INNER_H__
#define __DPDK_INNER_H__

#include "type.h"

struct numa_to_cpu {
    uint8_t numa_id;
    uint8_t hw_numa_id;
    uint8_t count;
    uint8_t hw_cpu_id[NUMA_PER_CPU_MAX];
    uint8_t cpu_id[NUMA_PER_CPU_MAX];
};

struct cpu_to_numa {
    uint8_t hw_cpu_id;
    uint8_t cpu_id;
    uint8_t numa_cpu_id;
    uint8_t numa_id;
    uint8_t hw_numa_id;
};

struct numa_cpu {
    int numa_count;
    int cpu_count;
    struct numa_to_cpu n2c[NUMA_MAX];
    struct cpu_to_numa c2n[CPU_MAX];
};

extern void dpdk_numa_cpu_init(int, int);

#endif // __DPDK_INNER_H__