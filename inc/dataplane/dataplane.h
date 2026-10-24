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

enum DP_CONFIG {
    DP_CONFIG_IFACE,
    DP_CONFIG_IP4_TABLE,
    DP_CONFIG_IP6_TABLE,
    DP_CONFIG_RS_TABLE,
    DP_CONFIG_POOL_TABLE,
    DP_CONFIG_SNAT_TABLE,
    DP_CONFIG_VS_TABLE,
    DP_CONFIG_ARP_TABLE,
    DP_CONFIG_ROUTE4_TABLE,
    DP_CONFIG_NDP_TABLE,
    DP_CONFIG_ROUTE6_TABLE,

    DP_CONFIG_MAX,
};

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
    void *table[DP_CONFIG_MAX];
} ALIGNED(CACHE_LINE);

struct thread_ctx {
    uint64_t version;
    void **pp_tables[DP_CONFIG_MAX];
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
    struct pkt_classifier *pc;
    struct stats *stats;
    struct {
        uint8_t numa_id; // NUMA index in your application (0-based)
        uint8_t cpu_id; // *Logical CPU (lcore) index used by the thread (0-based)
        uint8_t numa_cpu_id; // Thread index within the NUMA node (0-based)
        uint8_t hw_numa_id; // Actual system NUMA node ID (as reported by the OS)
        uint8_t hw_cpu_id; // Actual system CPU/core ID (OS-level, may not start from 0)
    };

    uint64_t off_time_us;
    uint64_t timer_cycles;

    void *message_ring;
    void *frag_handle;
    struct thread_ctx *ctx;
    struct thread_config *tc; // Pointer to the configuration for this NUMA node
    void *indirect_pool;
    void *pktmbuf_pool;
} ALIGNED(CACHE_LINE);

extern int dp_startup(void *arg);

static INLINE void dp_config_update(struct dataplane *dp)
{
    uint64_t version = 0;
    struct thread_ctx *ctx = dp->ctx;
    struct thread_config *tc = dp->tc;

    version = __atomic_load_n(&tc->version, __ATOMIC_ACQUIRE);
    if (ctx->version == version) {
        return;
    }

    UNROLL_LOOP_8(i, 0, DP_CONFIG_MAX, *ctx->pp_tables[i] = tc->table[i]);

    __atomic_store_n(&ctx->version, version, __ATOMIC_RELEASE);
}

static INLINE void dp_config_synchronize(struct dataplane *dp[], int count, int new_ver)
{
    for (int i = 0; i < count; i++) {
        struct thread_config *tc = dp[i]->tc;
        __atomic_store_n(&tc->version, new_ver, __ATOMIC_RELEASE);
    }

    for (int i = 0; i < count; i++) {
        struct thread_ctx *ctx = dp[i]->ctx;

        while (__atomic_load_n(&ctx->version, __ATOMIC_ACQUIRE) != new_ver) {
            PAUSE();
        }
    }

    return;
}

#endif // __DATAPLANE_H__