 /*****************************************************************************
 * filename: dataplane.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "timer.h"
#include "notify.h"
#include "dpdk_ip.h"
#include "protocol.h"
#include "dpdk_rcu.h"
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

// Thread-Local Storage
__thread uint8_t tls_thread_id;
__thread struct dataplane *tls_dp;
__thread struct pkt_store *tls_arp;
__thread struct pkt_store *tls_ipv4;
__thread struct pkt_store *tls_ipv6;
__thread struct pkt_store *tls_icmp;
__thread struct pkt_store *tls_icmp6;
__thread struct pkt_store *tls_tcp;
__thread struct pkt_store *tls_notify;
__thread struct pkt_store *tls_drop;
__thread struct pkt_store *tls_pending;
__thread struct pkt_store *tls_cache;
__thread struct pkt_tx *tls_tx;
__thread struct thread_config *tls_th_cfg;
__thread uint64_t tls_rx_offload[DPDK_ETHPORT_MAX];
__thread uint64_t tls_tx_offload[DPDK_ETHPORT_MAX];

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

    nc = dpdk_malloc(sizeof(*nc));
    if (nc == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    iface = dpdk_malloc(sizeof(struct iface) + nic_count * sizeof(uint16_t));
    if (UNLIKELY(iface == NULL)) {
        LOG_ERROR("OOM.");
        dpdk_free(nc);
        return NULL;
    }

    iface->nums = nic_count;
    for (int i = 0; i < nic_count; i++) {
        iface->port[i] = i;
    }

    ipv4_manage = l3_thread_startup(hw_numa_id, nic_count);
    if (UNLIKELY(ipv4_manage == NULL)) {
        dpdk_free(iface);
        dpdk_free(nc);
        return NULL;
    }

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
    tls_thread_id = tls_dp->cpu_id;
    tls_arp = &tls_dp->pc->arp;
    tls_ipv4 = &tls_dp->pc->ipv4;
    tls_ipv6 = &tls_dp->pc->ipv6;
    tls_icmp = &tls_dp->pc->icmp;
    tls_icmp6 = &tls_dp->pc->icmp6;
    tls_tcp = &tls_dp->pc->tcp;
    tls_drop = &tls_dp->pc->drop;
    tls_notify = &tls_dp->pc->notify;
    tls_pending = &tls_dp->pc->pending;
    tls_cache = &tls_dp->pc->cache;
    tls_tx = tls_dp->pc->tx;
    tls_th_cfg = tls_dp->tc;
    tls_dp->mtu = 1500;

    for (int i = 0; i < DPDK_ETHPORT_MAX; i++) {
        tls_rx_offload[i] = dpdk_port_rx_offload_get(i);
        tls_tx_offload[i] = dpdk_port_tx_offload_get(i);
    }
}

static INLINE void _dp_init(void *arg)
{
    void * const*pool = NULL;
    struct root *root = arg;
    char name[NOTICE_NAME_MAX] = "";

    tls_dp = dpdk_malloc(sizeof(*tls_dp));
    if (UNLIKELY(tls_dp == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }
    memset(tls_dp, 0, sizeof(*tls_dp));

    tls_dp->hz_per_second = dpdk_timer_hz();
    dpdk_thread_info(&tls_dp->numa_id,
                     &tls_dp->numa_cpu_id,
                     &tls_dp->cpu_id,
                     &tls_dp->hw_numa_id,
                     &tls_dp->hw_cpu_id);
    tls_thread_id = tls_dp->cpu_id;

    tls_dp->rcu = dpdk_rcu_get(tls_dp->numa_id, tls_dp->numa_cpu_id);
    if (tls_dp->rcu == NULL) {
        LOG_ERROR("RCU init failure.");
        goto _quit;
    }

    tls_dp->tc = _dp_tc_create(root->hw_info.nic_count, tls_dp->hw_numa_id);
    if (tls_dp->tc == NULL) {
        goto _quit;
    }

    snprintf(name, sizeof(name), "NOTICE_POOL_THREAD_%03d", tls_dp->cpu_id);
    tls_dp->pktmbuf_pool = dpdk_pool_pktmbuf_get_by_numa(tls_dp->numa_id);
    if (tls_dp->pktmbuf_pool == NULL) {
        LOG_ERROR("Failure cpu_id(%d) dpdk_pool_pktmbuf_get_by_numa", tls_dp->cpu_id);
        goto _quit;
    }

    tls_dp->indirect_pool = dpdk_indirect_pool_pktmbuf_get_by_numa(tls_dp->numa_id);
    if (tls_dp->indirect_pool == NULL) {
        LOG_ERROR("Failure cpu_id(%d) dpdk_indirect_pool_pktmbuf_get_by_numa", tls_dp->cpu_id);
        goto _quit;
    }

    tls_dp->frag_handle = dpdk_ip_frag_table_create(tls_dp->hz_per_second, tls_dp->hw_numa_id);
    if (UNLIKELY(tls_dp->frag_handle == NULL)) {
        goto _quit;
    }

    tls_dp->pc = _dp_pkt_classifier_create(root->hw_info.nic_count);
    if (tls_dp->pc == NULL) {
        goto _quit;
    }

    tls_dp->protocol = protocol_create(root->hw_info.nic_count, tls_dp);
    if (tls_dp->protocol == NULL) {
        goto _quit;
    }

    snprintf(name, sizeof(name), "NOTICE_RING_THREAD_%03d", tls_dp->cpu_id);
    tls_dp->notice_ring = dpdk_ring_ms_create(name, NOTICE_NUMS, tls_dp->hw_numa_id);
    if (tls_dp->notice_ring == NULL) {
        goto _quit;
    }

    tls_dp->stats = dpdk_malloc(sizeof(*tls_dp->stats));
    if (tls_dp->stats == NULL) {
        LOG_ERROR("OOM");
        goto _quit;
    }

    if (timer_init(arg) != 0 || notify_init(arg) != 0) {
        goto _quit;
    }

    dpdk_thread_set_name(tls_dp->numa_id, (uint8_t)tls_dp->cpu_id);

    _dp_thread_local_var_init();

    atomic_store(&root->dpdk_thread[tls_dp->cpu_id], tls_dp);

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
    // TODO 2025-07-14
    if (tls_dp != NULL) {
        dpdk_free(tls_dp->stats);
        dpdk_ring_destroy(tls_dp->notice_ring);
        dpdk_ip_frag_table_destroy(tls_dp->frag_handle);
        protocol_destroy(tls_dp->protocol);
        _dp_pkt_classifier_destroy(tls_dp->pc);
        _dp_tc_destroy(tls_dp->tc);
        dpdk_free(tls_dp);
    }

    exit(EXIT_FAILURE);
}

static INLINE void _dp_time_update(void)
{
    tls_dp->timer_cycles = dpdk_timer_cycles();
    tls_dp->off_time = tls_dp->timer_cycles / tls_dp->hz_per_second;
}

static INLINE void _dp_mbuf_send(const uint16_t ports[], int count)
{
    struct rte_eth_stats stat = {0};

    for (int i = 0; i < count; i++) {
        uint16_t port = ports[i];
        struct pkt_tx *tx = &tls_tx[port];

        if (tx->count != 0) {
            int nums = dpdk_pktmbuf_tx(port, tls_thread_id, tx->data, tx->count);
            if (UNLIKELY(nums != tx->count)) {
                dpdk_pktmbuf_push(tx->data + nums, tx->count - nums);
            }

            tx->count = 0;
        }
    }
}

static INLINE void _dp_mbuf_drop(void)
{
    if (tls_drop->count != 0) {
        dpdk_pktmbuf_push(tls_drop->data, tls_drop->count);
        tls_drop->count = 0;
    }
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
        iface = rcu_dereference(tls_th_cfg->iface);

        ports = iface->port;
        port_nums = iface->nums;
        count_rx_per = ARR_NUMS(mbuf) / port_nums;

        _dp_time_update();

        for (int i = 0; i < DP_LOOP_MAX; i++) {
            count = 0;
            for (int j = 0; j < port_nums; j++) {
                count += dpdk_pktmbuf_rx(ports[j], tls_thread_id, mbuf + count, count_rx_per);
            }

            if (count != 0) {
                l2_process(mbuf, count);

                l3_process();

                // l4_process();
            }

            notify_do();
            timer_check();

            _dp_mbuf_send(ports, port_nums);
            _dp_mbuf_drop();
        }

        dpdk_rcu_quiescent(tls_dp->rcu, tls_dp->numa_cpu_id);
    }

    // TODO fini
    return 0;
}