/************************************************
 * filename: dpdk_pool.c
 * function:
 * description:
 ***********************************************/

#include <time.h>
#include <stdio.h>

#include "log.h"
#include "type.h"
#include "macro.h"
#include "dpdk_type.h"
#include "dpdk_core.h"
#include "dpdk_inner.h"
#include "dpdk_common.h"

#define DPDK_PKTMBUF_CACHE_SIZE 256
#define DPDK_INDIRECT_PKTMBUF_MAX 8192

struct dpdk_pool_st {
    void *pktmbuf_pool[NUMA_MAX];
    void *indirect_pool[NUMA_MAX];
};

static struct dpdk_pool_st s_dpdk_pool;

static void _dpdk_pool_pktmbuf_destroy(void)
{
    void **tmp = s_dpdk_pool.pktmbuf_pool;
    void **indirect_tmp = s_dpdk_pool.indirect_pool;

    for (int i = 0; tmp[i] != NULL; i++) {
        rte_mempool_free(tmp[i]);
        rte_mempool_free(indirect_tmp[i]);
        tmp[i] = NULL;
        indirect_tmp[i] = NULL;
    }
}

int dpdk_pool_pktmbuf_create(void)
{
    struct timespec spec = {0};
    char name[CACHE_LINE] = "";
    void *tmp[NUMA_MAX] = {NULL};
    struct numa_cpu *nc = NULL;
    struct numa_to_cpu *n2c = NULL;
    void **pool = s_dpdk_pool.pktmbuf_pool;
    void **indirect_pool = s_dpdk_pool.indirect_pool;

    nc = dpdk_numa_cpu_get();
    n2c = nc->n2c;

    clock_gettime(CLOCK_MONOTONIC, &spec);

    for (int i = 0; i < nc->numa_count; i++) {
        snprintf(name, sizeof(name), "PKTMBUF_%lu_%02d", spec.tv_sec, i);
        tmp[i] = rte_pktmbuf_pool_create(name,
                                         n2c[i].count * DPDK_MAX_DESCRIPTORS_PER_CPU,
                                         DPDK_PKTMBUF_CACHE_SIZE,
                                         sizeof(struct dpdk_headroom),
                                         DPDK_DATA_LEN_MAX,
                                         n2c[i].hw_numa_id);
        if (UNLIKELY(tmp[i] == NULL)) {
            LOG_ERROR("Failure NUMA(%d) rte_pktmbuf_pool_create: %s", i, strerror(rte_errno));
            goto _quit;
        }

        pool[i] = tmp[i];

        snprintf(name, sizeof(name), "INDIRECT_%lu_%02d", spec.tv_sec, i);
        tmp[i] = rte_pktmbuf_pool_create(name,
                                         n2c[i].count * DPDK_INDIRECT_PKTMBUF_MAX,
                                         64,
                                         0,
                                         0,
                                         n2c[i].hw_numa_id);
        if (UNLIKELY(tmp[i] == NULL)) {
            LOG_ERROR("Failure NUMA(%d) rte_pktmbuf_pool_create: %s", i, strerror(rte_errno));
            goto _quit;
        }

        indirect_pool[i] = tmp[i];
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

void *dpdk_indirect_pool_pktmbuf_get_by_numa(int numa_id)
{
    if (numa_id >= NUMA_MAX) {
        return NULL;
    }

    return s_dpdk_pool.indirect_pool[numa_id];
}