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
    uint64_t version;
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
    uint64_t version;
    struct thread_config *tc; // Pointer to the configuration for this NUMA node
    void *pktmbuf_pool;
    void *notice_ring;
    struct pkt_classifier *pc;
    void *protocol;
    struct stats *stats;
    uint64_t hz_per_second;
    uint64_t off_time;
    struct {
        uint8_t numa_id; // NUMA index in your application (0-based)
        uint8_t numa_cpu_id; // Thread index within the NUMA node (0-based)
        uint8_t cpu_id; // *Logical CPU (lcore) index used by the thread (0-based)
        uint8_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
        uint8_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)
    };
} ALIGNED(CACHE_LINE);

struct hw_info {
    int numa_count;
    int cpu_count;
    int nic_count;
    uint64_t total_memory;
    uint64_t hugepage_size;
};

struct root {
    bool inited;
    uint64_t startup_time;
    struct hw_info hw_info;
    struct dataplane *dpdk_thread[CPU_MAX];
};

extern __thread uint8_t thread_id;
extern __thread struct dataplane *dp;
extern __thread struct pkt_store *arp_mbuf;
extern __thread struct pkt_store *ipv4_mbuf;
extern __thread struct pkt_store *ipv6_mbuf;
extern __thread struct pkt_store *icmp_mbuf;
extern __thread struct pkt_store *icmp6_mbuf;
extern __thread struct pkt_store *tcp_mbuf;
extern __thread struct pkt_store *notify_mbuf;
extern __thread struct pkt_store *drop_mbuf;
// Temporary bug: it must be freed immediately after use to avoid affecting the next module.
extern __thread struct pkt_store *pending_mbuf;
extern __thread struct pkt_store *cache_mbuf;
extern __thread struct pkt_tx *tx_mbuf;
extern __thread struct thread_config *th_cfg;

#endif // __TYPE_H__