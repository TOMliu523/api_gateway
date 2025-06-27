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

static struct dpdk_rcu *g_rcu[RTE_MAX_NUMA_NODES];

int dpdk_rcu_create(void)
{
    int ret = 0;
    int hw_numa = 0;
    size_t size = 0;
    int thread_count = 0;
    struct numa_cpu *nc = NULL;
    struct dpdk_rcu *rcu = NULL;

    nc = dpdk_numa_cpu_get();
    for (int i = 0; i < nc->numa_count; i++) {
        hw_numa = nc->n2c[i].hw_numa_id;
        thread_count = nc->n2c[i].count;

        size = rte_rcu_qsbr_get_memsize(thread_count);
        if (size == (size_t)-1) {
            LOG_ERROR("Failure rte_rcu_qsbr_get_memsize: %s", rte_strerror(rte_errno));
            dpdk_rcu_destroy();
            return -1;
        }

        rcu = dpdk_malloc_numa(size, hw_numa);
        if (rcu == NULL) {
            LOG_ERROR("Failure dpdk_malloc_numa: OOM");
            dpdk_rcu_destroy();
            return -1;
        }

        ret = rte_rcu_qsbr_init(rcu, thread_count);
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
    struct dpdk_rcu *rcu = NULL;

    nc = dpdk_numa_cpu_get();
    for (int i = 0; i < nc->numa_count; i++) {
        if (g_rcu[i] != NULL) {
            dpdk_free(g_rcu[i]);
            g_rcu[i] = NULL;
        }
    }
}

struct dpdk_rcu *dpdk_rcu_get(int numa_id, int thread_id)
{
    void *rcu = NULL;

    rcu = g_rcu[numa_id];
    if (rcu == NULL) {
        return NULL;
    }

    dpdk_rcu_thread_register(rcu, thread_id);
    dpdk_rcu_thread_online(rcu, thread_id);

    return rcu;
}