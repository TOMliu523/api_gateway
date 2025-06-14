/************************************************
 * filename: dpdk_init.c
 * function:
 * description:
 ***********************************************/

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_errno.h>
#include <rte_cycles.h>
#include <rte_random.h>
#include <rte_thread.h>
#include <rte_ethdev.h>
#include <rte_memory.h>
#include <rte_version.h>

#include "log.h"
#include "macro.h"

#define DPDK_1M (RTE_PGSIZE_2M / 2)

static int _dpdk_memory_info(const struct rte_memseg_list *msl, const struct rte_memseg *ms, void *arg)
{
    if (msl != NULL) {
        LOG_INFO("socket_id = %d, socket total len = %luMB, hugepage_size = %uMB, version = %d",
                 msl->socket_id, msl->len/ DPDK_1M, msl->page_sz / DPDK_1M, msl->version);
    }

    if (ms != NULL) {
        LOG_INFO("socket_id = %d, socket segment len = %luMB, hugepage_size = %uMB, nchannel = %d, nrank = %d",
                 ms->socket_id, ms->len / DPDK_1M, ms->hugepage_sz / DPDK_1M, ms->nchannel, ms->nrank);
    }

    return 0;
}

static INLINE void _dpdk_info(void)
{
    // Display basic DPDK runtime and hardware details
    LOG_INFO("DPDK VERSION: %s", rte_version());
    LOG_INFO("SYSTEM NUMA COUNT: %d", rte_socket_count());
    LOG_INFO("DPDK CPU COUNT: %d", rte_lcore_count());
    LOG_INFO("DPDK NIC COUNT: %d", rte_eth_dev_count_avail());
    rte_memseg_walk(_dpdk_memory_info, NULL);
}

int dpdk_init(int argc, char *argv[])
{
    int ret = 0;

    RUNTIME_ASSERT(argv != NULL);

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure. %s ", rte_strerror(rte_errno));
        return -1;
    }

    rte_srand(rte_rdtsc());

    _dpdk_info();

    return 0;
}

void dpdk_thread_startup(void *f, void *arg)
{
    RUNTIME_ASSERT(f != NULL);

    LOG_INFO("STARTUP DPDK THREAD.");
    rte_eal_mp_remote_launch(f, arg, CALL_MAIN);
}