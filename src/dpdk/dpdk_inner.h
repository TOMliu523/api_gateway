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
    int inited;
    int numa_count;
    int cpu_count;
    struct numa_to_cpu n2c[NUMA_MAX];
    struct cpu_to_numa c2n[CPU_MAX];
};

extern int dpdk_numa_cpu_init(int);
extern int dpdk_cpu_count_get(void);
extern int dpdk_numa_count_get(void);
extern struct numa_cpu *dpdk_numa_cpu_get(void);

#endif // __DPDK_INNER_H__