/************************************************
 * filename: dataplane.h
 * function:
 * description:
 ***********************************************/

#ifndef __DATAPLANE_H__
#define __DATAPLANE_H__

#include <stdint.h>

#include "type.h"
#include "macro.h"

struct dataplane {
    struct numa_config *nc; // Pointer to the configuration for this NUMA node

    uint8_t numa_id; // NUMA index in your application (0-based)
    uint8_t numa_cpu_id; // Thread index within the NUMA node (0-based)
    uint16_t dpdk_cpu_id; // Logical CPU (lcore) index used by the thread (0-based)
    uint16_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
    uint16_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)
} ALIGNED(CACHE_LINE);

int dp_startup(void *arg);

#endif // __DATAPLANE_H__