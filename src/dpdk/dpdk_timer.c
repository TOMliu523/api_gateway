/*****************************************************************************
 * filename: dpdk_timer.c
 * function:
 * description:
 ****************************************************************************/

#include <rte_errno.h>

#include "log.h"
#include "dpdk_core.h"
#include "dpdk_inner.h"
#include "dpdk_timer.h"

struct dpdk_timer_ctl {
    void *timer_pool[NUMA_MAX];
};

static struct dpdk_timer_ctl s_timer_ctl;

int dpdk_timer_startup(void)
{
    int ret = 0;
    char name[CACHE_LINE] = "";
    struct numa_cpu *nc = NULL;
    struct dpdk_timer_ctl *ctl = &s_timer_ctl;

    ret = dpdk_timer_subsystem_init();
    if (ret != 0) {
        LOG_ERROR("Init timer failure: %s", strerror(-rte_errno));
        return -1;
    }

    nc = dpdk_numa_cpu_get();
    for (int i = 0; i < nc->numa_count; i++) {
        struct numa_to_cpu *n2c = &nc->n2c[i];

        snprintf(name, sizeof(name), "DPDK_TIMER_%d", i);
        ctl->timer_pool[i] = dpdk_pool_mm_create(name,
                                                 n2c->count * DPDK_MAX_TIMER_PER_CPU,
                                                 sizeof(struct dpdk_timer),
                                                 n2c->hw_numa_id);
        if (ctl->timer_pool[i] == NULL) {
            goto _quit;
        }
    }

    return 0;

_quit:
    for (int i = 0; i < nc->numa_count; i++) {
        dpdk_pool_destroy(ctl->timer_pool[i]);
    }

    dpdk_timer_subsystem_fini();
    return -1;
}

void dpdk_timer_shutdown(void)
{
}

void *dpdk_timer_pool_get(int numa_id)
{
    return s_timer_ctl.timer_pool[numa_id];
}