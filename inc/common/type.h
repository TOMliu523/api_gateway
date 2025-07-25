/*****************************************************************************
 * filename: type.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>

#include "dpdk_type.h"

#ifndef NUMA_MAX
#define NUMA_MAX RTE_MAX_NUMA_NODES
#endif // NUMA_MAX

#ifndef NUMA_PER_CPU_MAX
#define NUMA_PER_CPU_MAX 64
#endif // NUMA_PER_CPU_MAX

#ifndef CPU_MAX
#define CPU_MAX RTE_MAX_LCORE
#endif // CPU_MAX

#ifndef MBUF_STORE_MAX
#define MBUF_STORE_MAX 2048
#endif // MBUF_STORE_PER_MAX

#ifndef MBUF_NOTIFY_MAX
#define MBUF_NOTIFY_MAX 128
#endif // MBUF_NOTIFY_MAX

/*
 * If a structure’s data is only updated by the configuration thread,
 * it should be stored in struct thread_config.
 * If the structure’s data is updated by both the data plane and the configuration thread,
 * it should be placed in the data plane.
 * After the configuration thread prepares the data,
 * it should make it easy for the data plane to quickly attach or apply the data.
 */
struct thread_config {
    void *iface;
    void *ipv4_manage;
    // ... other per-NUMA modules
} ALIGNED(CACHE_LINE);

struct stats {
    uint64_t rx_pkt_count;
} ALIGNED(CACHE_LINE);

struct pkt_store {
    int count;
    void *data[MBUF_STORE_MAX];
};

struct pkt_tx {
    int count;
    void *data[4 * MBUF_STORE_MAX];
};

struct pkt_classifier {
    struct pkt_store
        arp,
        ipv4,
        ipv6,
        icmp,
        icmp6,
        tcp,
        drop,
        notify,
        pending,
        cache;

    struct pkt_tx *tx;
};

struct dataplane {
    void *rcu;
    struct thread_config *tc; // Pointer to the configuration for this NUMA node
    void *pktmbuf_pool;
    void *indirect_pool;
    void *notice_ring;
    void *frag_handle;
    struct pkt_classifier *pc;
    void *protocol;
    struct stats *stats;
    uint64_t hz_per_second;
    uint64_t timer_cycles;
    uint64_t off_time;
    struct {
        uint8_t numa_id; // NUMA index in your application (0-based)
        uint8_t numa_cpu_id; // Thread index within the NUMA node (0-based)
        uint8_t cpu_id; // *Logical CPU (lcore) index used by the thread (0-based)
        uint8_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
        uint8_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)
    };
    uint16_t mtu;
} ALIGNED(CACHE_LINE);

struct hw_info {
    int numa_count;
    int cpu_count;
    int nic_count;
    uint64_t total_memory[NUMA_MAX];
    uint64_t hugepage_size[NUMA_MAX];
};

struct root {
    bool inited;
    uint64_t startup_time;
    struct hw_info hw_info;
    struct dataplane *dpdk_thread[CPU_MAX];
};

extern __thread uint8_t tls_thread_id;
extern __thread struct dataplane *tls_dp;
extern __thread struct pkt_store *tls_arp;
extern __thread struct pkt_store *tls_ipv4;
extern __thread struct pkt_store *tls_ipv6;
extern __thread struct pkt_store *tls_icmp;
extern __thread struct pkt_store *tls_icmp6;
extern __thread struct pkt_store *tls_tcp;
extern __thread struct pkt_store *tls_notify;
extern __thread struct pkt_store *tls_drop;
// Temporary bug: it must be freed immediately after use to avoid affecting the next module.
extern __thread struct pkt_store *tls_pending;
extern __thread struct pkt_store *tls_cache;
extern __thread struct pkt_tx *tls_tx;
extern __thread struct thread_config *tls_th_cfg;
extern __thread uint64_t tls_rx_offload[DPDK_ETHPORT_MAX];
extern __thread uint64_t tls_tx_offload[DPDK_ETHPORT_MAX];

#endif // __TYPE_H__
