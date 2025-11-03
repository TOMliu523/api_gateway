 /*****************************************************************************
 * filename: dataplane.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "l2.h"
#include "log.h"
#include "tcp.h"
#include "timer.h"
#include "notify.h"
#include "dpdk_ip4.h"
#include "dpdk_ip6.h"
#include "dpdk_rcu.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_init.h"
#include "dataplane.h"
#include "dpdk_common.h"
#include "thread_config.h"

#define DP_LOOP_MAX 256
#define NOTICE_NAME_MAX 64
// Set it large enough to ensure it won’t become full.
#define NOTICE_NUMS (256 * 1024)
// avoid putting excessive pressure on L1/L2 cache by reading too many packets in one go

#define DP_FLUSH_EVERY 512
#define DP_PORT_LOOP_PER_MAX 16

// Thread-Local Storage
__thread uint8_t tlv_numa_id;
__thread uint8_t tlv_thread_id;
__thread uint8_t tlv_hw_numa_id;
__thread struct dataplane *tlv_dp;
__thread struct pkt_store *tlv_arp;
__thread struct pkt_store *tlv_ip4;
__thread struct pkt_store *tlv_ip6;
__thread struct pkt_store *tlv_icmp;
__thread struct pkt_store *tlv_icmp6;
__thread struct pkt_store *tlv_tcp4;
__thread struct pkt_store *tlv_tcp6;
__thread struct pkt_store *tlv_notify;
__thread struct pkt_store *tlv_drop;
__thread struct pkt_store *tlv_pending;
__thread struct pkt_store *tlv_cache;
__thread struct pkt_store *tlv_cache1;
__thread struct pkt_store *tlv_cache2;
__thread struct pkt_store *tlv_cache3;
__thread struct pkt_tx *tlv_tx;
__thread struct thread_config *tlv_th_cfg;
__thread uint64_t tlv_rx_offload[DPDK_ETHPORT_MAX];
__thread uint64_t tlv_tx_offload[DPDK_ETHPORT_MAX];

static void *_dp_ctx_init(int hw_numa_id)
{
    struct thread_ctx *ctx = NULL;

    ctx = dpdk_malloc_numa(sizeof(*ctx), hw_numa_id);
    if (UNLIKELY(ctx == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(ctx, 0, sizeof(*ctx));
    return ctx;
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
    tlv_numa_id = tlv_dp->numa_id;
    tlv_thread_id = tlv_dp->cpu_id;
    tlv_hw_numa_id = tlv_dp->hw_numa_id;
    tlv_arp = &tlv_dp->pc->arp;
    tlv_ip4 = &tlv_dp->pc->ip4;
    tlv_ip6 = &tlv_dp->pc->ip6;
    tlv_icmp = &tlv_dp->pc->icmp;
    tlv_icmp6 = &tlv_dp->pc->icmp6;
    tlv_tcp4 = &tlv_dp->pc->tcp4;
    tlv_tcp6 = &tlv_dp->pc->tcp6;
    tlv_drop = &tlv_dp->pc->drop;
    tlv_notify = &tlv_dp->pc->notify;
    tlv_pending = &tlv_dp->pc->pending;
    tlv_cache = &tlv_dp->pc->cache;
    tlv_cache1 = &tlv_dp->pc->cache1;
    tlv_cache2 = &tlv_dp->pc->cache2;
    tlv_cache3 = &tlv_dp->pc->cache3;
    tlv_tx = tlv_dp->pc->tx;
    tlv_th_cfg = tlv_dp->tc;
    tlv_dp->mtu = 1500;

    for (int i = 0; i < DPDK_ETHPORT_MAX; i++) {
        tlv_rx_offload[i] = dpdk_port_rx_offload_get(i);
        tlv_tx_offload[i] = dpdk_port_tx_offload_get(i);
    }
}

static INLINE void _dp_init(void *arg)
{
    struct root *root = arg;
    char name[NOTICE_NAME_MAX] = "";

    tlv_dp = dpdk_malloc(sizeof(*tlv_dp));
    if (UNLIKELY(tlv_dp == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }
    memset(tlv_dp, 0, sizeof(*tlv_dp));

    dpdk_thread_info(&tlv_dp->numa_id,
                     &tlv_dp->numa_cpu_id,
                     &tlv_dp->cpu_id,
                     &tlv_dp->hw_numa_id,
                     &tlv_dp->hw_cpu_id);
    tlv_thread_id = tlv_dp->cpu_id;

    tlv_dp->rcu = dpdk_rcu_get(tlv_dp->cpu_id);
    if (tlv_dp->rcu == NULL) {
        LOG_ERROR("RCU init failure.");
        goto _quit;
    }

    tlv_dp->ctx = _dp_ctx_init(tlv_dp->hw_numa_id);
    if (tlv_dp->ctx == NULL) {
        goto _quit;
    }

    tlv_dp->tc = tc_init(tlv_dp->ctx, root->hw_info.nic_count, tlv_dp->hw_numa_id, tlv_dp->cpu_id);
    if (tlv_dp->tc == NULL) {
        goto _quit;
    }

    snprintf(name, sizeof(name), "NOTICE_POOL_THREAD_%03d", tlv_dp->cpu_id);
    tlv_dp->pktmbuf_pool = dpdk_pool_pktmbuf_get_by_numa(tlv_dp->numa_id);
    if (tlv_dp->pktmbuf_pool == NULL) {
        LOG_ERROR("Failure cpu_id(%d) dpdk_pool_pktmbuf_get_by_numa", tlv_dp->cpu_id);
        goto _quit;
    }

    tlv_dp->indirect_pool = dpdk_indirect_pool_pktmbuf_get_by_numa(tlv_dp->numa_id);
    if (tlv_dp->indirect_pool == NULL) {
        LOG_ERROR("Failure cpu_id(%d) dpdk_indirect_pool_pktmbuf_get_by_numa", tlv_dp->cpu_id);
        goto _quit;
    }

    tlv_dp->frag_handle = dpdk_ip_frag_table_create(dpdk_timer_hz(), tlv_dp->hw_numa_id);
    if (UNLIKELY(tlv_dp->frag_handle == NULL)) {
        goto _quit;
    }

    tlv_dp->pc = _dp_pkt_classifier_create(root->hw_info.nic_count);
    if (tlv_dp->pc == NULL) {
        goto _quit;
    }

    snprintf(name, sizeof(name), "NOTICE_RING_THREAD_%03d", tlv_dp->cpu_id);
    tlv_dp->notice_ring = dpdk_ring_ms_create(name, NOTICE_NUMS, tlv_dp->hw_numa_id);
    if (tlv_dp->notice_ring == NULL) {
        goto _quit;
    }

    tlv_dp->stats = dpdk_malloc(sizeof(*tlv_dp->stats));
    if (tlv_dp->stats == NULL) {
        LOG_ERROR("OOM");
        goto _quit;
    }

    if (timer_init(arg) != 0 || notify_init(arg) != 0) {
        goto _quit;
    }

    dpdk_thread_set_name(tlv_dp->numa_id, (uint8_t)tlv_dp->cpu_id);

    _dp_thread_local_var_init();

    atomic_store(&root->dpdk_thread[tlv_dp->cpu_id], tlv_dp);

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
    if (tlv_dp != NULL) {
        tc_fini(tlv_dp->tc);
        dpdk_free(tlv_dp->stats);
        dpdk_ring_destroy(tlv_dp->notice_ring);
        dpdk_ip_frag_table_destroy(tlv_dp->frag_handle);
        _dp_pkt_classifier_destroy(tlv_dp->pc);
        dpdk_free(tlv_dp->ctx);
        dpdk_free(tlv_dp);
        tlv_dp = NULL;
    }

    exit(EXIT_FAILURE);
}

static INLINE void _dp_time_update(double inv_hz, double inv_hz_ms)
{
    tlv_dp->timer_cycles = dpdk_timer_cycles();
    tlv_dp->off_time = tlv_dp->timer_cycles * inv_hz;
    tlv_dp->off_time_ms = tlv_dp->timer_cycles * inv_hz_ms;
}

static INLINE void _dp_mbuf_send(const uint16_t ports[], int count)
{
    int cnt = 0;
    uint16_t port = 0;
    struct pkt_tx *tx = NULL;

    UNROLL_LOOP_8(i, count, {
        port = ports[i];
        tx = &tlv_tx[port];
        cnt = tx->count;

        if (cnt != 0) {
            int nums = dpdk_pktmbuf_tx(port, tlv_thread_id, tx->data, cnt);
            if (UNLIKELY(nums != cnt)) {
                dpdk_pktmbuf_push(tx->data + nums, cnt - nums);
            }

            tx->count = 0;
        }
    });
}

static INLINE void _dp_mbuf_drop(void)
{
    int cnt = tlv_drop->count;
    if (cnt != 0) {
        dpdk_pktmbuf_push(tlv_drop->data, cnt);
        tlv_drop->count = 0;
    }
}

static INLINE void _dp_l2_process(void *data[], int count)
{
    l2_process(data, count);
}

static INLINE void _dp_l3_process(UNUSED void *data[], UNUSED int count)
{
    int ip4_count = 0;
    int ip6_count = 0;

    ip4_count = tlv_ip4->count;
    if (ip4_count != 0) {
        ip4_process(tlv_ip4->data, ip4_count);
        tlv_ip4->count = 0;
    }

    ip6_count = tlv_ip6->count;
    if (ip6_count != 0) {
        ip6_process(tlv_ip6->data, ip6_count);
        tlv_ip6->count = 0;
    }
}

static INLINE void _dp_l4_process(UNUSED void *data[], UNUSED int count)
{
    int tcp4_count = 0;
    int tcp6_count = 0;

    tcp4_count = tlv_tcp4->count;
    if (tcp4_count != 0) {
        tcp4_process(tlv_tcp4->data, tcp4_count);
        tlv_tcp4->count = 0;
    }

    tcp6_count = tlv_tcp6->count;
    if (tcp6_count != 0) {
        tcp6_process(tlv_tcp6->data, tcp6_count);
        tlv_tcp6->count = 0;
    }
}

// Dispatch new configuration before the data plane runs again.
static INLINE void _dp_thread_config_refresh(void)
{
    struct thread_ctx *ctx = tlv_dp->ctx;
    // if (ctx->version != tlv_th_cfg->version) {

        // *ctx->pp_iface = tlv_th_cfg->iface;
        *ctx->pp_ip4_table = tlv_th_cfg->ip4_table;
        *ctx->pp_ip6_table = tlv_th_cfg->ip6_table;
        *ctx->pp_rs_table = tlv_th_cfg->rs_table;
        *ctx->pp_pool_table = tlv_th_cfg->pool_table;
        *ctx->pp_snat_table = tlv_th_cfg->snat_table;
        *ctx->pp_vs_table = tlv_th_cfg->vs_table;
        *ctx->pp_arp_table = tlv_th_cfg->arp_table;
        *ctx->pp_route4_table = tlv_th_cfg->route4_table;
        *ctx->pp_ndp_table = tlv_th_cfg->ndp_table;
        *ctx->pp_route6_table = tlv_th_cfg->route6_table;

        // ctx->version = tlv_th_cfg->version;
    // }
}

int dp_startup(void *arg)
{
    int port_nums = 0;
    double inv_hz = 0;
    double inv_hz_ms = 0;
    const uint16_t *ports = NULL;
    const struct iface *iface = NULL;
    const uint64_t hz_per_second = dpdk_timer_hz();

    static __thread void *data[DP_MBUF_MAX] = {NULL};

    /*
     * If initialization fails, then exit
     * — just like how a system cannot start if hardware self-checks fail.
     */
    _dp_init(arg);

    inv_hz = 1.0 / (double) hz_per_second;
    inv_hz_ms = 1000.0 / (double) hz_per_second;

    for (;;) {
        _dp_thread_config_refresh();

        iface = tlv_th_cfg->iface;
        ports = iface->port;
        port_nums = iface->nums;

        if (UNLIKELY(port_nums == 0)) {
            PAUSE();
            continue;
        }

        _dp_time_update(inv_hz, inv_hz_ms);

        for (int i = 0; i < DP_LOOP_MAX; i++) {
            for (int j = 0; j < port_nums; j++) {
                int count = 0;
                int total = 0;
                int budget = DP_PORT_LOOP_PER_MAX;

                do {
                    count = dpdk_pktmbuf_rx(ports[j], tlv_thread_id, data, DP_MBUF_MAX);
                    if (count == 0) break;

                    _dp_l2_process(data, count);
                    _dp_l3_process(data, count);
                    _dp_l4_process(data, count);

                    total += count;
                    if (total >= DP_FLUSH_EVERY) {
                        _dp_mbuf_send(&ports[j], 1);
                        _dp_mbuf_drop();
                    }
                } while (--budget > 0);
            }

            notify_do();
            timer_check();

            _dp_mbuf_send(ports, port_nums);
            _dp_mbuf_drop();
        }

        dpdk_rcu_quiescent(tlv_dp->rcu, 0);
    }

    // TODO fini
    return 0;
}