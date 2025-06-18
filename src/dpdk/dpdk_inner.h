/************************************************
 * filename: dpdk_inner.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INNER_H__
#define __DPDK_INNER_H__

#include "type.h"

struct numa_to_cpu {
    int numa_id;
    int hw_numa_id;
    int count;
    int hw_cpu_id[NUMA_CPU_MAX];
    int cpu_lcore[NUMA_CPU_MAX];
};

struct cpu_to_numa {
    int hw_cpu_id;
    int dpdk_cpu_id;
    int numa_cpu_id;
    int dpdk_numa_id;
    int hw_numa_id;
};

struct numa_cpu {
    int numa_count;
    int cpu_count;
    struct numa_to_cpu n2c[NUMA_MAX];
    struct cpu_to_numa c2n[NUMA_MAX * NUMA_CPU_MAX];
};

extern void dpdk_numa_cpu_init(int, int);

#endif // __DPDK_INNER_H__