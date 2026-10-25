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
#include "dpdk_ip.h"
#include "dpdk_core.h"
#include "dpdk_init.h"
#include "dataplane.h"
#include "dpdk_common.h"

#define DATAPLANE_NAME_LEN 16
#define DATAPLANE_MBUF_NUMS 64
#define DATAPLANE_BATCH_NUMS 64
#define DATAPLANE_PORT_LOOP_MAX 16
#define DATAPLANE_MSG_MAX (65536 * 2)

static __thread struct dataplane *dataplane;

static INLINE void _dp_time_update(double inv_hz_us)
{
    dataplane->off_time_us = dataplane->timer_cycles * inv_hz_us;
}

static void _dp_ctx_init(struct thread_ctx *ctx)
{

}

static void _dp_fini(void)
{
    if (dataplane->pc != NULL) {
        free(dataplane->pc);
    }

    if (dataplane->tc != NULL) {
        free(dataplane->tc);
    }

    if (dataplane->stats != NULL) {
        free(dataplane->stats);
    }

    if (dataplane->message_ring != NULL) {
        dpdk_ring_destroy(dataplane->message_ring);
    }

    if (dataplane->frag_handle != NULL) {
        dpdk_ip_frag_table_destroy(dataplane->frag_handle);
    }

    if (dataplane->ctx != NULL) {
        free(dataplane->ctx);
    }

    if (dataplane != NULL) {
        free(dataplane);
    }

    dataplane = NULL;
}

static void _dp_init(struct data_root *root)
{
    int ret = 0;
    struct hw_info *info = root->info;
    char name[DATAPLANE_NAME_LEN] = "";

    ret = alloc_align((void **)&dataplane, sizeof(*dataplane));
    if (ret != 0) {
        LOG_ERROR("OOM.");
        return;
    }

    memset(dataplane, 0, sizeof(*dataplane));
    dpdk_thread_info(&dataplane->numa_id, &dataplane->cpu_id, &dataplane->numa_cpu_id, &dataplane->hw_numa_id, &dataplane->numa_cpu_id);

    ret = alloc_align((void **)&dataplane->tc, sizeof(*dataplane->tc));
    if (ret != 0) {
        LOG_ERROR("OOM");
        goto _quit;
    }

    memset(dataplane->tc, 0, sizeof(*dataplane->tc));

    ret = alloc_align((void **)&dataplane->pc, sizeof(*dataplane->pc));
    if (ret != 0) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    dataplane->pktmbuf_pool = dpdk_pool_pktmbuf_get_by_numa(dataplane->numa_id);
    if (dataplane->pktmbuf_pool == NULL) {
        LOG_ERROR("Inner error.");
        goto _quit;
    }

    dataplane->indirect_pool = dpdk_indirect_pool_pktmbuf_get_by_numa(dataplane->numa_id);
    if (dataplane->indirect_pool == NULL) {
        LOG_ERROR("Inner error.");
        goto _quit;
    }

    ret = alloc_align((void **)&dataplane->stats, sizeof(*dataplane->stats));
    if (ret != 0) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    dataplane->timer_cycles = dpdk_timer_cycles();

    snprintf(name, sizeof(name), "MSG_RING_%d", dataplane->cpu_id);
    dataplane->message_ring = dpdk_ring_ss_create(name, DATAPLANE_MSG_MAX, dataplane->hw_numa_id);
    if (dataplane->message_ring == NULL) {
        goto _quit;
    }

    dataplane->frag_handle = dpdk_ip_frag_table_create(dpdk_timer_hz(), dataplane->hw_numa_id);
    if (dataplane->frag_handle == NULL) {
        goto _quit;
    }

    ret = alloc_align((void **)&dataplane->ctx, sizeof(*dataplane->ctx));
    if (ret != 0) {
        LOG_ERROR();
    }

    memset(dataplane->ctx, 0, sizeof(*dataplane->ctx));
    _dp_ctx_init(dataplane->ctx);

    dpdk_thread_set_name(dataplane->numa_id, dataplane->cpu_id);
    atomic_store(&root->dataplane[dataplane->cpu_id], dataplane);
    check_and_wait(root->dataplane, info->cpu_count);

    LOG_INFO("Worker thread %d startup SUCCESS!!!", (int)dataplane->cpu_id);
    return;

_quit:
    _dp_fini();
    exit(EXIT_FAILURE); // TODO process quit
}

int dp_startup(void *arg)
{
    int ports_nums = 0;
    double inv_hz_us = 0.0;
    struct data_root *root = (struct data_root *) arg;

    static __thread void *mbufs[DATAPLANE_MBUF_NUMS] = {NULL};

    _dp_init(root);
    inv_hz_us = 1000000.0 / (double) dpdk_timer_hz();

    for (;;) {
        dp_config_update(dataplane);

        _dp_time_update(inv_hz_us);
        for (int i = 0; i < DATAPLANE_BATCH_NUMS; i++) {
            for (int j = 0; j < ports_nums; j++) {
                int count = 0;
                // int total = 0;
                int budget = DATAPLANE_PORT_LOOP_MAX;

                do {
                    count = dpdk_pktmbuf_rx(0, dataplane->cpu_id, mbufs, ARR_NUMS(mbufs));
                    if (count == 0) {
                        break;
                    }
                } while (--budget > 0);
            }

            // timer_check();
        }
    }

    return 0;
}