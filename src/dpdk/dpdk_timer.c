/*****************************************************************************
 * filename: dpdk_timer.c
 * function:
 * description:
 ****************************************************************************/

#include <time.h>
#include <rte_errno.h>

#include "log.h"
#include "timeout.h"
#include "dpdk_core.h"
#include "dpdk_inner.h"
#include "dpdk_timer.h"

struct dpdk_timer_ctl {
    void *timer_pool[NUMA_MAX];
};

static struct dpdk_timer_ctl s_timer_ctl;

int dpdk_timer_start(void)
{
    struct timespec spec = {0};
    char name[CACHE_LINE] = "";
    struct numa_cpu *nc = NULL;
    struct dpdk_timer_ctl *ctl = &s_timer_ctl;

    clock_gettime(CLOCK_MONOTONIC, &spec);
    nc = dpdk_numa_cpu_get();

    for (int i = 0; i < nc->numa_count; i++) {
        struct numa_to_cpu *n2c = &nc->n2c[i];

        snprintf(name, sizeof(name), "DPDK_TIMER_%lu_%d", spec.tv_sec, i);
        ctl->timer_pool[i] = dpdk_pool_mm_create(name,
                                                 n2c->count * DPDK_MAX_TIMER_PER_CPU,
                                                 sizeof(struct timeout),
                                                 n2c->hw_numa_id);
        if (ctl->timer_pool[i] == NULL) {
            goto _quit;
        }
    }

    return 0;

_quit:
    dpdk_timer_close();
    return -1;
}

void *dpdk_timer_thread_create(void)
{
    int error = 0;

    struct timeouts *tos = timeouts_open(TIMEOUT_mHZ, &error);
    if (tos == NULL) {
        LOG_ERROR("DPDK timer error: %s.", strerror(error));
        return NULL;
    }

    return tos;
}

void dpdk_timer_thread_destroy(void *arg)
{
    if (arg != NULL) {
        timeouts_close(arg);
    }
}

void dpdk_timer_close(void)
{
    struct dpdk_timer_ctl * ctl = &s_timer_ctl;
    for (int i = 0; i < NUMA_MAX; i++) {
        if (ctl->timer_pool[i] != NULL) {
            dpdk_pool_destroy(ctl->timer_pool[i]);
        }
    }
}

void *dpdk_timer_pool_get(int numa_id)
{
    return s_timer_ctl.timer_pool[numa_id];
}