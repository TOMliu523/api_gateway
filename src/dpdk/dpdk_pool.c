/************************************************
 * filename: dpdk_pool.c
 * function:
 * description:
 ***********************************************/

#include <stdio.h>

#include "log.h"
#include "type.h"
#include "macro.h"
#include "dpdk_type.h"
#include "dpdk_core.h"
#include "dpdk_inner.h"
#include "dpdk_common.h"

#define DPDK_PKTMBUF_CACHE_SIZE 256
#define DPDK_MAX_DESCRIPTORS_PER_CPU 100000

struct dpdk_pool_st {
    void *pktmbuf_pool[NUMA_MAX];
};

static struct dpdk_pool_st s_dpdk_pool;

static void _dpdk_pool_pktmbuf_destroy(void)
{
    void **tmp = s_dpdk_pool.pktmbuf_pool;

    for (int i = 0; tmp[i] != NULL; i++) {
        rte_mempool_free(tmp[i]);
        tmp[i] = NULL;
    }
}

int dpdk_pool_pktmbuf_create(void)
{
    char name[CACHE_LINE] = "";
    void *tmp[NUMA_MAX] = {NULL};
    struct numa_cpu *nc = NULL;
    struct numa_to_cpu *n2c = NULL;
    void **pool = s_dpdk_pool.pktmbuf_pool;

    nc = dpdk_numa_cpu_get();
    n2c = nc->n2c;

    for (int i = 0; i < nc->numa_count; i++) {
        snprintf(name, sizeof(name), "PKTMBUF_POOL_NUMA_%02d", i);
        tmp[i] = rte_pktmbuf_pool_create(name,
                                         n2c[i].count * DPDK_MAX_DESCRIPTORS_PER_CPU,
                                         DPDK_PKTMBUF_CACHE_SIZE,
                                         sizeof(struct dpdk_headroom),
                                         DPDK_DATA_LEN_MAX,
                                         n2c[i].hw_numa_id);
        if (tmp == NULL) {
            LOG_ERROR("Failure NUMA(%d) rte_pktmbuf_pool_create: %s", i, strerror(rte_errno));
            goto _quit;
        }

        pool[i] = tmp[i];
    }

    atexit(_dpdk_pool_pktmbuf_destroy);
    return 0;

_quit:
    _dpdk_pool_pktmbuf_destroy();
    return -1;
}

void * const *dpdk_pool_pktmbuf_get(void)
{
    return s_dpdk_pool.pktmbuf_pool;
}

void *dpdk_pool_pktmbuf_get_by_numa(int numa_id)
{
    if (numa_id >= NUMA_MAX) {
        return NULL;
    }

    return s_dpdk_pool.pktmbuf_pool[numa_id];
}