 /*****************************************************************************
 * filename: dataplane.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "rcu.h"
#include "type.h"
#include "atomic.h"
#include "dpdk_type.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_init.h"
#include "dataplane.h"
#include "dpdk_common.h"

#define DP_LOOP_MAX 32
#define DP_RX_MAX_PER 64
#define NOTICE_NAME_MAX 64
#define NOTICE_DATA_LEN 128
#define NOTICE_NUMS (64 * 1024)
#define DP_MBUF_MAX (DP_RX_MAX_PER * DPDK_ETHPORT_MAX)

static __thread struct dataplane dataplane;

static INLINE void _dp_init(void *arg)
{
    void * const*pool = NULL;
    struct root *root = arg;
    char name[NOTICE_NAME_MAX] = "";
    struct dataplane *dp = &dataplane;

    dpdk_thread_info(&dp->numa_id, &dp->numa_cpu_id, &dp->cpu_id, &dp->hw_numa_id, &dp->hw_cpu_id);
    LOG_INFO("hw_cpu_id = %d, cpu_lcore = %d, local_idx = %d, hw_numa_id = %d, numa_idx = %d",
             dp->hw_cpu_id, dp->cpu_id, dp->numa_cpu_id, dp->hw_numa_id, dp->numa_id);

    dp->nc = dpdk_malloc(sizeof(*dp->nc));
    if (dp->nc == NULL) {
        LOG_ERROR("Failure dpdk_malloc OOM.");
        goto _quit;
    }

    memset(dp->nc, 0, sizeof(*dp->nc));

    dp->nc->iface = dpdk_malloc(sizeof(struct iface));
    if (dp->nc->iface == NULL) {
        LOG_ERROR("Failure dpdk_malloc OOM.");
        goto _quit;
    }

    memset(dp->nc->iface, 0, sizeof(struct iface));

    snprintf(name, sizeof(name), "NOTICE_POOL_THREAD_%03d", dp->cpu_id);
    dp->pktmbuf = dpdk_pool_pktmbuf_get_by_numa(dp->numa_id);
    if (dp->pktmbuf == NULL) {
        LOG_ERROR("Failure numa_id(%d) dpdk_pool_pktmbuf_get_by_numa", dp->numa_id);
        goto _quit;
    }

    snprintf(name, sizeof(name), "NOTICE_RING_THREAD_%03d", dp->cpu_id);
    dp->notice_ring = dpdk_ring_ms_create(name, NOTICE_NUMS, dp->hw_numa_id);
    if (dp->notice_ring == NULL) {
        goto _quit;
    }

    dp->stats = dpdk_malloc(sizeof(*dp->stats));
    if (dp->stats == NULL) {
        LOG_ERROR("OOM");
        goto _quit;
    }

    root->dpdk_thread[dp->numa_cpu_id] = dp;
    dpdk_thread_set_name(dp->numa_id, (uint8_t)dp->cpu_id);

    return;

_quit:
    if (dp->stats != NULL) {
        dpdk_free(dp->stats);
    }

    if (dp->notice_ring != NULL) {
        dpdk_ring_destroy(dp->notice_ring);
    }

    if (dp->nc != NULL) {
        if (dp->nc->iface != NULL) {
            dpdk_free(dp->nc->iface);
        }

        dpdk_free(dp->nc);
    }

    exit(EXIT_FAILURE);
}

static void _dp_show(struct dpdk_mbuf *mbufs[], int max)
{
    struct dpdk_eth *eth = NULL;
    struct dpdk_mbuf *mbuf = NULL;
#define MAC_FORMAT "%02hX:%02hX:%02hX:%02hX:%02hX:%02hX"

    for (int i = 0; i < max; i++) {
        mbuf = mbufs[i];

        eth = dpdk_pktmbuf_eth(mbuf);
        LOG_INFO("type: %X, dst: " MAC_FORMAT ", src: " MAC_FORMAT,
                  htons(eth->ether_type),
                  eth->dst_addr.addr_bytes[0], eth->dst_addr.addr_bytes[1], eth->dst_addr.addr_bytes[2],
                  eth->dst_addr.addr_bytes[3], eth->dst_addr.addr_bytes[4], eth->dst_addr.addr_bytes[5],
                  eth->src_addr.addr_bytes[0], eth->src_addr.addr_bytes[1], eth->src_addr.addr_bytes[2],
                  eth->src_addr.addr_bytes[3], eth->src_addr.addr_bytes[4], eth->src_addr.addr_bytes[5]);
    }
}

static INLINE void _dp_channel(struct dataplane *dp)
{
    unsigned count = 0;
    struct dpdk_mbuf *mbuf = NULL;
    static __thread void *mbufs[DP_RX_MAX_PER] = {NULL};

    count = dpdk_ring_sc_pop(dp->notice_ring, mbufs, ARR_NUMS(mbufs));
    for (unsigned i = 0; i < count; i++) {
        mbuf = mbufs[i];
        dpdk_pktmbuf_tx(mbuf->port, dp->cpu_id, &mbuf, 1);
    }
}

int dp_startup(void *arg)
{
    int count = 0;
    struct dataplane *dp = NULL;
    const struct iface *iface = NULL;
    static __thread struct dpdk_mbuf *mbuf[DP_MBUF_MAX] = {NULL};

    _dp_init(arg);
    dp = &dataplane;

    for (;;) {
        rcu_read_lock(&dataplane);

        for (int i = 0; i < DP_LOOP_MAX; i++) {
            count = 0;
            iface = rcu_dereference(dp->nc->iface);

            for (int j = 0; j < iface->nums; j++) {
                count += dpdk_pktmbuf_rx(iface->port[j], dp->cpu_id, mbuf + count, DP_RX_MAX_PER);
            }

            if (count != 0) {
                _dp_show(mbuf, count);
            }

            // TODO channel
            _dp_channel(dp);
            // TODO timer
        }

        rcu_read_unlock(&dataplane);
    }

    // TODO fini

    return 0;
}