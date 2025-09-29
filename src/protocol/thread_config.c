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
#include "thread_config.h"

static struct iface *_tc_iface_init(int hw_numa_id)
{
    struct iface *iface = NULL;
    size_t size = sizeof(struct iface);

    iface = dpdk_malloc_numa(size, hw_numa_id);
    if (UNLIKELY(iface == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(iface, 0, size);
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

    rserver_fini(tc->rs_table);
    ip6_manage_destroy(tc->ip6_manage);
    ip4_manage_destroy(tc->ip4_manage);
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

    tc->iface = _tc_iface_init(hw_numa_id);
    if (UNLIKELY(tc->iface == NULL)) {
        goto _quit;
    }

    tc->ip4_manage = ip4_manage_startup(nic_count, hw_numa_id);
    if (UNLIKELY(tc->ip4_manage == NULL)) {
        goto _quit;
    }

    tc->ip6_manage = ip6_manage_startup(nic_count, hw_numa_id);
    if (UNLIKELY(tc->ip6_manage == NULL)) {
        goto _quit;
    }

    tc->rs_table = rserver_init(hw_numa_id);
    if (UNLIKELY(tc->rs_table == NULL)) {
        goto _quit;
    }

    return tc;

_quit:
    tc_fini(tc);
    return NULL;
}