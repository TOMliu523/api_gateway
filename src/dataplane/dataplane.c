/*****************************************************************************
 * filename: dataplane.c
 * function:
 * description: Worker Entry Point
 ****************************************************************************/

#define _GNU_SOURCE
#include <pthread.h>

#include "top.h"
#include "log.h"
#include "common.h"
#include "dpdk_core.h"
#include "dpdk_init.h"
#include "dataplane.h"

static __thread struct dataplane *dataplane;

static void _dataplane_fini(struct dataplane *dataplen)
{

}

static void _dataplane_init(struct data_root *root)
{
    int ret = 0;
    struct hw_info *info = root->info;

    ret = alloc_align_system((void **)&dataplane, sizeof(*dataplane));
    if (ret != 0) {
        LOG_ERROR("OOM.");
        return;
    }

    memset(dataplane, 0, sizeof(*dataplane));
    dpdk_thread_info(&dataplane->numa_id, &dataplane->cpu_id, &dataplane->numa_cpu_id, &dataplane->hw_numa_id, &dataplane->numa_cpu_id);

    dataplane->tc = NULL;
    ret = alloc_align_system((void **)&dataplane->pc, sizeof(*dataplane->pc));
    if (ret != 0) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    dataplane->pktmbuf_pool = dpdk_pool_pktmbuf_get_by_numa(dataplane->numa_id);
    if (dataplane->pktmbuf_pool == NULL) {
        LOG_ERROR("Inner error.");
        goto _quit;
    }

    ret = alloc_align_system((void **)&dataplane->stats, sizeof(*dataplane->stats));
    if (ret != 0) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    dpdk_thread_set_name(dataplane->numa_id, dataplane->cpu_id);
    atomic_store(&root->dataplane[dataplane->cpu_id], dataplane);
    check_and_wait(root->dataplane, info->cpu_count);

    return;

_quit:
    _dataplane_fini(dataplane);
    dataplane = NULL;
    pthread_exit(NULL);
}

int dp_startup(void *arg)
{
    struct data_root *root = (struct data_root *) arg;

    _dataplane_init(root);

    for (;;) {

    }

    return 0;
}