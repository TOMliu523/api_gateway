 /*****************************************************************************
 * filename: dataplane.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "l2.h"
#include "l3.h"
#include "log.h"
#include "rcu.h"
#include "timer.h"
#include "notify.h"
#include "protocol.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_init.h"
#include "dataplane.h"
#include "dpdk_common.h"

#define DP_LOOP_MAX 64
#define NOTICE_NAME_MAX 64
// Set it large enough to ensure it won’t become full.
#define NOTICE_NUMS (64 * 1024)
#define DP_MBUF_MAX (MBUF_STORE_MAX / 2)

__thread uint8_t thread_id;
__thread struct dataplane *dp;
__thread struct pkt_store *arp_mbuf;
__thread struct pkt_store *ipv4_mbuf;
__thread struct pkt_store *ipv6_mbuf;
__thread struct pkt_store *icmp_mbuf;
__thread struct pkt_store *icmp6_mbuf;
__thread struct pkt_store *tcp_mbuf;
__thread struct pkt_store *notify_mbuf;
__thread struct pkt_store *drop_mbuf;
__thread struct pkt_store *pending_mbuf;
__thread struct pkt_store *cache_mbuf;
__thread struct pkt_tx *tx_mbuf;
__thread struct thread_config *th_cfg;

static void _dp_tc_destroy(struct thread_config *nc)
{
    if (nc != NULL) {
        dpdk_free(nc->iface);
        dpdk_free(nc);
    }
}

static void *_dp_tc_create(int nic_count, int hw_numa_id)
{
    struct iface *iface = NULL;
    struct thread_config *nc = NULL;
    struct ipv4_manage *ipv4_manage = NULL;

    iface = dpdk_malloc(sizeof(struct iface) + nic_count * sizeof(uint16_t));
    if (UNLIKELY(iface == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    iface->nums = nic_count;
    for (int i = 0; i < nic_count; i++) {
        iface->port[i] = i;
    }

    ipv4_manage = l3_thread_ipv4_create(hw_numa_id);
    if (UNLIKELY(ipv4_manage == NULL)) {
        LOG_ERROR("OOM.");
        dpdk_free(iface);
        return NULL;
    }

    nc = dpdk_malloc(sizeof(*nc));
    if (nc == NULL) {
        LOG_ERROR("OOM.");
        dpdk_free(iface);
        dpdk_free(ipv4_manage);
        return NULL;
    }

    nc->version = 0;
    nc->iface = iface;
    nc->ipv4_manage = ipv4_manage;

    return nc;
}

static void _dp_pkt_classifier_destroy(void *ptr)
{
    struct pkt_classifier* pc = ptr;

    if (pc == NULL) {
        return;
    }

    if (pc->tx != NULL) {
        dpdk_free(pc->tx);
    }

    dpdk_free(pc);
}

static void *_dp_pkt_classifier_create(int nic_count)
{
    struct pkt_classifier *pc = NULL;

    pc = dpdk_malloc(sizeof(*pc));
    if (UNLIKELY(pc == NULL)) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(pc, 0, sizeof(*pc));

    pc->tx = dpdk_malloc(nic_count * sizeof(struct pkt_tx));
    if (UNLIKELY(pc->tx == NULL)) {
        LOG_ERROR("OOM.");
        dpdk_free(pc);
        return NULL;
    }

    memset(pc->tx, 0, nic_count * sizeof(struct pkt_tx));
    return pc;
}

static INLINE void _dp_thread_local_var_init(void)
{
    thread_id = dp->cpu_id;
    arp_mbuf = &dp->pc->arp;
    ipv4_mbuf = &dp->pc->ipv4;
    ipv6_mbuf = &dp->pc->ipv6;
    icmp_mbuf = &dp->pc->icmp;
    icmp6_mbuf = &dp->pc->icmp6;
    tcp_mbuf = &dp->pc->tcp;
    drop_mbuf = &dp->pc->drop;
    notify_mbuf = &dp->pc->notify;
    pending_mbuf = &dp->pc->pending;
    cache_mbuf = &dp->pc->cache;
    tx_mbuf = dp->pc->tx;
    th_cfg = dp->tc;
}

static INLINE void _dp_init(void *arg)
{
    void * const*pool = NULL;
    struct root *root = arg;
    char name[NOTICE_NAME_MAX] = "";

    dp = dpdk_malloc(sizeof(*dp));
    if (UNLIKELY(dp == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }
    memset(dp, 0, sizeof(*dp));

    dpdk_thread_info(&dp->numa_id, &dp->numa_cpu_id, &dp->cpu_id, &dp->hw_numa_id, &dp->hw_cpu_id);
    thread_id = dp->cpu_id;

    dp->tc = _dp_tc_create(root->hw_info.nic_count, dp->hw_numa_id);
    if (dp->tc == NULL) {
        goto _quit;
    }

    snprintf(name, sizeof(name), "NOTICE_POOL_THREAD_%03d", dp->cpu_id);
    dp->pktmbuf_pool = dpdk_pool_pktmbuf_get_by_numa(dp->numa_id);
    if (dp->pktmbuf_pool == NULL) {
        LOG_ERROR("Failure cpu_id(%d) dpdk_pool_pktmbuf_get_by_numa", dp->cpu_id);
        goto _quit;
    }

    dp->pc = _dp_pkt_classifier_create(root->hw_info.nic_count);
    if (dp->pc == NULL) {
        goto _quit;
    }

    dp->protocol = protocol_create(root->hw_info.nic_count, dp);
    if (dp->protocol == NULL) {
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

    if (timer_init(arg) != 0 || notify_init(arg) != 0) {
        goto _quit;
    }

    dp->hz_per_second = dpdk_timer_hz();
    dpdk_thread_set_name(dp->numa_id, (uint8_t)dp->cpu_id);

    _dp_thread_local_var_init();

    atomic_store(&root->dpdk_thread[dp->cpu_id], dp);

    /*
     * Must wait for all threads to complete initialization before starting business logic,
     * to avoid issues such as notifications arriving before threads are ready.
     */
    for (;;) {
        int count = 0;
        for (int i = 0; i < root->hw_info.cpu_count; i++) {
            if (atomic_load(&root->dpdk_thread[i]) != NULL) {
                count += 1;
            }
        }

        if (count == root->hw_info.cpu_count) {
            atomic_store(&root->inited, true);
            break;
        }
    }

    return;

_quit:
    if (dp != NULL) {
        dpdk_free(dp->stats);
        dpdk_ring_destroy(dp->notice_ring);
        protocol_destroy(dp->protocol);
        _dp_pkt_classifier_destroy(dp->pc);
        _dp_tc_destroy(dp->tc);
        dpdk_free(dp);
    }

    exit(EXIT_FAILURE);
}

static INLINE void _dp_time_update(void)
{
    dp->off_time = dpdk_timer_cycles() / dp->hz_per_second;
}

int dp_startup(void *arg)
{
    int count = 0;
    int port_nums = 0;
    int count_rx_per = 0;
    const uint16_t *ports = NULL;
    const struct iface *iface = NULL;

    static __thread void *mbuf[DP_MBUF_MAX] = {NULL};

    /*
     * If initialization fails, then exit
     * — just like how a system cannot start if hardware self-checks fail.
     */
    _dp_init(arg);

    for (;;) {
        rcu_read_lock(dp);

        iface = rcu_dereference(th_cfg->iface);

        ports = iface->port;
        port_nums = iface->nums;
        count_rx_per = ARR_NUMS(mbuf) / port_nums;

        _dp_time_update();

        for (int i = 0; i < DP_LOOP_MAX; i++) {
            count = 0;
            for (int j = 0; j < port_nums; j++) {
                count += dpdk_pktmbuf_rx(ports[j], thread_id, mbuf + count, count_rx_per);
            }

            if (count != 0) {
                l2_do(mbuf, count);

                /*if (pc->ip4.count != 0) {
                    // ip4_do();
                    pc->ip4.count = 0;

                    if (pc->icmp.count != 0) {
                        // icmp_do();
                        pc->icmp.count = 0;
                    }
                }

                if (pc->ip6.count != 0) {
                    // ip6_do();
                    pc->ip6.count = 0;

                    if (pc->icmp6.count != 0) {
                        // icmp6_do();
                        pc->icmp6.count = 0;
                    }
                }

                if (pc->tcp.count != 0) {
                    // tcp_do();
                    pc->tcp.count = 0;
                }*/
            }

            notify_do();
            timer_check();

            for (int j = 0; j < port_nums; j++) {
                uint16_t port = ports[j];
                struct pkt_tx *tx = &tx_mbuf[port];

                if (tx->count != 0) {
                    int nums = dpdk_pktmbuf_tx(port, thread_id, tx->data, tx->count);
                    if (UNLIKELY(nums != tx->count)) {
                        dpdk_pktmbuf_push(tx->data + nums, tx->count - nums);
                    }
                    tx->count = 0;
                }
            }

            if (drop_mbuf->count != 0) {
                dpdk_pktmbuf_push(drop_mbuf->data, drop_mbuf->count);
                drop_mbuf->count = 0;
            }
        }
    }

    // TODO fini
    return 0;
}