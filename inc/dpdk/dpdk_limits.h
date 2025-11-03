/*****************************************************************************
 * filename: dpdk_limits.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_LIMITS_H__
#define __DPDK_LIMITS_H__

#include <rte_build_config.h>

// port
#ifndef DPDK_ETHPORT_MAX
#define DPDK_ETHPORT_MAX RTE_MAX_ETHPORTS
#endif // DPDK_ETHPORT_MAX

// numa
#ifndef NUMA_MAX
#define NUMA_MAX RTE_MAX_NUMA_NODES
#endif // NUMA_MAX

// numa per cpu
#ifndef NUMA_PER_CPU_MAX
#define NUMA_PER_CPU_MAX 64
#endif // NUMA_PER_CPU_MAX

// cpu max
#ifndef CPU_MAX
#define CPU_MAX RTE_MAX_LCORE
#endif // CPU_MAX

// per cpu descriptors
#ifndef DPDK_MAX_DESCRIPTORS_PER_CPU
#define DPDK_MAX_DESCRIPTORS_PER_CPU 500000
#endif // DPDK_MAX_DESCRIPTORS_PER_CPU

#ifndef DPDK_MAX_CONN_PER_CPU
#define DPDK_MAX_CONN_PER_CPU 2000000
#endif // DPDK_MAX_CONN_PER_CPU

#ifndef DPDK_MAX_TIMER_PER_CPU
#define DPDK_MAX_TIMER_PER_CPU (DPDK_MAX_CONN_PER_CPU)
#endif // DPDK_MAX_TIMER_PER_CPU

#ifndef DP_MBUF_MAX
#define DP_MBUF_MAX 64
#endif // DP_MBUF_MAX

#ifndef DP_BATCH_MAX
#define DP_BATCH_MAX DP_MBUF_MAX
#endif // DP_BATCH_MAX

#ifndef DP_RSERVER_MAX
#define DP_RSERVER_MAX 50000
#endif // DP_RSERVER_MAX

#ifndef DP_POOL_MAX
#define DP_POOL_MAX 10000
#endif // DP_POOL_MAX

#ifndef DP_SNAT_POOL_MAX
#define DP_SNAT_POOL_MAX 10000
#endif // DP_SNAT_POOL_MAX

#ifndef DP_VSERVER_MAX
#define DP_VSERVER_MAX 10000
#endif // DP_VSERVER_MAX

#ifndef DP_TCP_CONN_MAX_PER_THREAD
#define DP_TCP_CONN_MAX_PER_THREAD 5000000
#endif // DP_TCP_CONN_MAX_PER_THREAD

#endif // __DPDK_LIMITIS_H__