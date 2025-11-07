/*****************************************************************************
 * filename: type.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>

#include "dpdk_type.h"

#ifndef MBUF_STORE_MAX
#define MBUF_STORE_MAX 4096
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
    void *ip4_table;
    void *ip6_table;
    void *rs_table;
    void *pool_table;
    void *snat_table;
    void *vs_table;

    struct dpdk_mac *mac;

    void *arp_table;
    void *route4_table;
    void *ndp_table;
    void *route6_table;
    // ... other per-CPU modules
} ALIGNED(CACHE_LINE);

struct thread_ctx {
    uint64_t version;

    void **pp_iface;
    void **pp_ip4_table;
    void **pp_ip6_table;
    void **pp_rs_table;
    void **pp_pool_table;
    void **pp_snat_table;
    void **pp_vs_table;

    void **pp_arp_table;
    void **pp_route4_table;
    void **pp_ndp_table;
    void **pp_route6_table;
} ALIGNED(CACHE_LINE);

struct stats {
    uint64_t rx_pkt_count;

    struct ip4_stat {
        uint64_t ip4_cksum_fail;
        uint64_t ip4_version_fail;
        uint64_t ip4_ttl_fail;
    } ip4_stat;
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
        ip4,
        ip6,
        icmp,
        icmp6,
        tcp4,
        tcp6,
        drop,
        notify,
        pending,
        // Try to use it all within one interface instead of carrying it over to the next interface.
        cache, cache1, cache2, cache3;

    struct pkt_tx *tx;
};

struct dataplane {
    void *rcu;
    struct thread_config *tc; // Pointer to the configuration for this NUMA node
    struct pkt_classifier *pc;
    void *pktmbuf_pool;
    struct {
        uint8_t numa_id; // NUMA index in your application (0-based)
        uint8_t numa_cpu_id; // Thread index within the NUMA node (0-based)
        uint8_t cpu_id; // *Logical CPU (lcore) index used by the thread (0-based)
        uint8_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
        uint8_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)
    };
    uint16_t mtu;

    uint64_t off_time;
    uint64_t off_time_ms;
    uint64_t timer_cycles;

    struct stats *stats;
    void *indirect_pool;
    void *notice_ring;
    void *frag_handle;

    struct thread_ctx *ctx;
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

extern __thread uint8_t tlv_numa_id;
extern __thread uint8_t tlv_thread_id;
extern __thread uint8_t tlv_hw_numa_id;
extern __thread struct dataplane *tlv_dp;
extern __thread struct pkt_store *tlv_arp;
extern __thread struct pkt_store *tlv_ip4;
extern __thread struct pkt_store *tlv_ip6;
extern __thread struct pkt_store *tlv_icmp;
extern __thread struct pkt_store *tlv_icmp6;
extern __thread struct pkt_store *tlv_tcp4;
extern __thread struct pkt_store *tlv_tcp6;
extern __thread struct pkt_store *tlv_notify;
extern __thread struct pkt_store *tlv_drop;
// Temporary bug: it must be freed immediately after use to avoid affecting the next module.
extern __thread struct pkt_store *tlv_pending;
// Try to use it all within one interface instead of carrying it over to the next interface.
extern __thread struct pkt_store *tlv_cache;
extern __thread struct pkt_store *tlv_cache1;
extern __thread struct pkt_store *tlv_cache2;
extern __thread struct pkt_store *tlv_cache3;
extern __thread struct pkt_tx *tlv_tx;
extern __thread struct thread_config *tlv_th_cfg;
extern __thread uint64_t tlv_rx_offload[DPDK_ETHPORT_MAX];
extern __thread uint64_t tlv_tx_offload[DPDK_ETHPORT_MAX];

static INLINE bool tx_offload_f_test(uint16_t port, uint64_t f)
{
    return ((tlv_tx_offload[port] & f) != 0);
}

static INLINE bool rx_offload_f_test(uint16_t port, uint64_t f)
{
    return ((tlv_rx_offload[port] & f) != 0);
}

static INLINE void pktmbuf_drop(struct dpdk_mbuf *m)
{
    struct pkt_store *drop = tlv_drop;
    drop->data[drop->count++] = m;
}

static INLINE void pktmbuf_send(struct dpdk_mbuf *m)
{
    struct pkt_tx *tx = &tlv_tx[m->port];
    tx->data[tx->count++] = m;
}

#endif // __TYPE_H__