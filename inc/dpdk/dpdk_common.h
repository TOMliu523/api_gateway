/************************************************
 * filename: dpdk_common.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_COMMON_H__
#define __DPDK_COMMON_H__

extern int dpdk_cpu_count_get(void);
extern int dpdk_numa_count_get(void);
extern void *dpdk_numa_cpu_get(void);

#endif // __DPDK_COMMON_H__