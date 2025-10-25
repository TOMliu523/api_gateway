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
#include "rserver.h"
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

    rserver_thread_destroy(tc->rs_table);
    ip6_table_destroy(tc->ip6_table);
    ip4_table_destroy(tc->ip4_table);
    dpdk_free(tc->iface);

    dpdk_free(tc);
}

struct thread_config *tc_init(int nic_count, int hw_numa_id)
{
    struct thread_config *tc = NULL;

    tc = _tc_init(hw_numa_id);
    if (UNLIKELY(tc == NULL)) {
        return NULL;
    }

    tc->iface = _tc_iface_init(nic_count, hw_numa_id);
    if (UNLIKELY(tc->iface == NULL)) {
        goto _quit;
    }

    tc->ip4_table = ip4_table_startup(nic_count, hw_numa_id);
    if (UNLIKELY(tc->ip4_table == NULL)) {
        goto _quit;
    }

    tc->ip6_table = ip6_table_startup(nic_count, hw_numa_id);
    if (UNLIKELY(tc->ip6_table == NULL)) {
        goto _quit;
    }

    tc->rs_table = rserver_thread_create(hw_numa_id);
    if (UNLIKELY(tc->rs_table == NULL)) {
        goto _quit;
    }

    return tc;

_quit:
    tc_fini(tc);
    return NULL;
}