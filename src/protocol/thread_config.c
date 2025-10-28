/*****************************************************************************
 * filename: thread_config.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "l2.h"
#include "ip4.h"
#include "ip6.h"
#include "log.h"
#include "type.h"
#include "pool.h"
#include "route4.h"
#include "route6.h"
#include "rserver.h"
#include "vserver.h"
#include "snat_pool.h"
#include "dpdk_port.h"
#include "thread_config.h"

static struct iface *_tc_iface_init(int nic_count, int hw_numa_id)
{
    struct iface *iface = NULL;
    const struct port_info *port_info = NULL;
    size_t size = sizeof(struct iface) + nic_count * sizeof(uint16_t);

    iface = dpdk_malloc_numa(size, hw_numa_id);
    if (UNLIKELY(iface == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(iface, 0, size);

    port_info = dpdk_port_info_get();
    for (int i = 0; i < nic_count; i++) {
        iface->port[i] = port_info->info[i].port;
    }

    return iface;
}

static struct thread_config *_tc_init(int hw_numa_id)
{
    struct thread_config *tc = NULL;

    tc = dpdk_malloc_numa(sizeof(*tc), hw_numa_id);
    if (UNLIKELY(tc == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(tc, 0, sizeof(*tc));
    return tc;
}

void tc_fini(struct thread_config *tc)
{
    if (tc == NULL) {
        return;
    }

    dpdk_free(tc->iface);
    ip4_table_destroy(tc->ip4_table);
    ip6_table_destroy(tc->ip6_table);
    rserver_thread_destroy(tc->rs_table);
    pool_thread_destroy(tc->pool_table);
    snat_thread_destroy(tc->snat_table);
    vserver_thread_destroy(tc->vs_table);
    l2_thread_mac_destroy(tc->mac);
    l2_thread_arp_table_destroy(tc->arp_table);
    route4_thread_destroy(tc->route4_table);
    ip6_thread_ndp_table_destroy(tc->ndp_table);
    route6_thread_destroy(tc->route6_table);

    dpdk_free(tc);
}

struct thread_config *tc_init(void *arg, int nic_count, int hw_numa_id, int cpu_id)
{
    struct thread_ctx *ctx = arg;
    struct thread_config *tc = NULL;

    tc = _tc_init(hw_numa_id);
    if (UNLIKELY(tc == NULL)) {
        return NULL;
    }

    tc->iface = _tc_iface_init(nic_count, hw_numa_id);
    if (UNLIKELY(tc->iface == NULL)) {
        goto _quit;
    }

    tc->ip4_table = ip4_table_startup(&ctx->pp_ip4_table, nic_count, hw_numa_id);
    if (UNLIKELY(tc->ip4_table == NULL)) {
        goto _quit;
    }

    tc->ip6_table = ip6_table_startup(&ctx->pp_ip6_table, nic_count, hw_numa_id);
    if (UNLIKELY(tc->ip6_table == NULL)) {
        goto _quit;
    }

    tc->rs_table = rserver_thread_create(&ctx->pp_rs_table, hw_numa_id);
    if (UNLIKELY(tc->rs_table == NULL)) {
        goto _quit;
    }

    tc->pool_table = pool_thread_create(&ctx->pp_pool_table, hw_numa_id);
    if (UNLIKELY(tc->pool_table == NULL)) {
        goto _quit;
    }

    tc->snat_table = snat_thread_create(&ctx->pp_snat_pool, hw_numa_id);
    if (UNLIKELY(tc->snat_table == NULL)) {
        goto _quit;
    }

    tc->vs_table = vserver_thread_create(&ctx->pp_vs_table, hw_numa_id);
    if (UNLIKELY(tc->vs_table == NULL)) {
        goto _quit;
    }

    tc->mac = l2_thread_mac_create(nic_count);
    if (UNLIKELY(tc->mac == NULL)) {
        goto _quit;
    }

    tc->arp_table = l2_thread_arp_table_create(&ctx->pp_arp_table, nic_count, cpu_id, hw_numa_id);
    if (UNLIKELY(tc->arp_table == NULL)) {
        goto _quit;
    }

    tc->route4_table = route4_thread_create(&ctx->pp_route4_table, hw_numa_id);
    if (UNLIKELY(tc->route4_table == NULL)) {
        goto _quit;
    }

    tc->ndp_table = ip6_thread_ndp_table_create(&ctx->pp_ip6_table, nic_count, cpu_id, hw_numa_id);
    if (UNLIKELY(tc->ndp_table == NULL)) {
        goto _quit;
    }

    tc->route6_table = route6_thread_create(&ctx->pp_route6_table, hw_numa_id);
    if (UNLIKELY(tc->route6_table == NULL)) {
        goto _quit;
    }

    return tc;

_quit:
    tc_fini(tc);
    return NULL;
}