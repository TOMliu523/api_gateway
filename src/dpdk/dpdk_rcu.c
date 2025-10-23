/*****************************************************************************
 * filename: dpdk_rcu.c
 * function:
 * description:
 *              The RCU (Read-Copy-Update) library provides APIs for
 *              managing shared data in a safe, efficient,
 *              and scalable manner without locking readers.
 ****************************************************************************/

#include "log.h"
#include "dpdk_rcu.h"
#include "dpdk_inner.h"
#include "dpdk_common.h"

static struct dpdk_rcu *g_rcu[RTE_MAX_LCORE];

int dpdk_rcu_create(void)
{
    int ret = 0;
    size_t size = 0;
    int hw_numa_id = 0;
    struct numa_cpu *nc = NULL;
    struct dpdk_rcu *rcu = NULL;

    nc = dpdk_numa_cpu_get();
    for (int i = 0; i < nc->cpu_count; i++) {
        hw_numa_id = nc->c2n[i].hw_numa_id;

        size = rte_rcu_qsbr_get_memsize(1);
        if (size == (size_t)-1) {
            LOG_ERROR("Failure rte_rcu_qsbr_get_memsize: %s", rte_strerror(rte_errno));
            dpdk_rcu_destroy();
            return -1;
        }

        rcu = dpdk_malloc_numa(size, hw_numa_id);
        if (rcu == NULL) {
            LOG_ERROR("Failure dpdk_malloc_numa: OOM");
            dpdk_rcu_destroy();
            return -1;
        }

        ret = rte_rcu_qsbr_init(rcu, 1);
        if (ret != 0) {
            LOG_ERROR("Failure rte_rcu_qsbr_init: %s", rte_strerror(rte_errno));
            dpdk_free(rcu);
            dpdk_rcu_destroy();
            return -1;
        }

        g_rcu[i] = rcu;
    }

    return 0;
}

void dpdk_rcu_destroy(void)
{
    struct numa_cpu *nc = NULL;

    nc = dpdk_numa_cpu_get();
    for (int i = 0; i < nc->cpu_count; i++) {
        if (g_rcu[i] != NULL) {
            dpdk_free(g_rcu[i]);
            g_rcu[i] = NULL;
        }
    }
}

struct dpdk_rcu *dpdk_rcu_get(int cpu_id)
{
    void *rcu = NULL;

    rcu = g_rcu[cpu_id];
    if (rcu == NULL) {
        return NULL;
    }

    dpdk_rcu_thread_register(rcu, 0);
    dpdk_rcu_thread_online(rcu, 0);

    return rcu;
}