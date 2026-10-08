/*****************************************************************************
 * filename: dataplane.h
 * function:
 * description: Worker Entry Point
 ****************************************************************************/

#ifndef __DATAPLANE_H__
#define __DATAPLANE_H__

#include <stdint.h>

#include "macro.h"

#ifndef MBUF_STORE_MAX
#define MBUF_STORE_MAX 256
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
        l2,
        l3,
        tcp,
        udp,
        drop;

    struct pkt_tx tx;
};

struct dataplane {
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

extern int dp_startup(void *arg);

#endif // __DATAPLANE_H__