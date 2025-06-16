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
#include "type.h"
#include "macro.h"

#define DPDK_1M (RTE_PGSIZE_2M / 2)

static int _dpdk_memory_info(const struct rte_memseg_list *msl, const struct rte_memseg *ms, void *arg)
{
    struct hw_info *info = arg;

    if (msl != NULL) {
        info->hugepage_size = msl->page_sz;
        info->total_size += msl->len;
    }

    return 0;
}

static INLINE void _dpdk_info(struct hw_info *info)
{
    info->numa_nums = rte_socket_count();
    info->cpu_nums = rte_lcore_count();
    info->nic_nums = rte_eth_dev_count_avail();
    rte_memseg_walk(_dpdk_memory_info, info);
}

int dpdk_init(int argc, char *argv[], void *output)
{
    int ret = 0;

    RUNTIME_ASSERT(argv != NULL);

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure. %s ", rte_strerror(rte_errno));
        return -1;
    }

    rte_srand(rte_rdtsc());

    _dpdk_info(output);

    return 0;
}

void dpdk_thread_startup(void *f, void *arg)
{
    RUNTIME_ASSERT(f != NULL);

    LOG_INFO("STARTUP DPDK THREAD.");
    rte_eal_mp_remote_launch(f, arg, CALL_MAIN);
}

void dpdk_thread_set_name(const char *name)
{
    rte_thread_set_name(rte_thread_self(), name);
}