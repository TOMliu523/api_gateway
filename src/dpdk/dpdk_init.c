/************************************************
 * filename: dpdk_init.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <stdint.h>
#include <net/if.h>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_lcore.h>
#include <rte_errno.h>
#include <rte_memcpy.h>
#include <rte_cycles.h>
#include <rte_random.h>
#include <rte_thread.h>
#include <rte_ethdev.h>
#include <rte_memory.h>
#include <rte_ethdev.h>
#include <rte_version.h>

#include "log.h"
#include "type.h"
#include "macro.h"
#include "dpdk_rcu.h"
#include "dpdk_type.h"
#include "dpdk_init.h"
#include "dpdk_port.h"
#include "dpdk_inner.h"
#include "dpdk_common.h"

#define DPDK_1M (RTE_PGSIZE_2M / 2)

static int _dpdk_memory_info(const struct rte_memseg_list *msl, const struct rte_memseg *ms, void *arg)
{
    struct hw_info *info = arg;

    if (msl != NULL) {
        info->hugepage_size = msl->page_sz;
        info->total_memory += msl->len;
    }

    return 0;
}

static INLINE void _dpdk_info(struct hw_info *info)
{
    info->numa_count = rte_socket_count();
    info->cpu_count = rte_lcore_count();
    info->nic_count = rte_eth_dev_count_avail();
    rte_memseg_walk(_dpdk_memory_info, info);
}

void dpdk_thread_startup(void *f, void *arg)
{
    RUNTIME_ASSERT(f != NULL);

    LOG_INFO("STARTUP DPDK THREAD.");
    rte_eal_mp_remote_launch(f, arg, CALL_MAIN);
}

void dpdk_thread_set_name(uint8_t numa_idx, uint8_t cpu_id)
{
    char name[RTE_THREAD_NAME_SIZE + 1] = "";

    snprintf(name, sizeof(name), "DATAPLANE_%02d%03d", numa_idx, cpu_id);
    rte_thread_set_name(rte_thread_self(), name);
}

void dpdk_thread_info(uint8_t *numa_idx, uint8_t *local_idx, uint8_t *cpu_lcore, uint8_t *hw_numa_id, uint8_t *hw_cpu_id)
{
    struct numa_cpu *nc = dpdk_numa_cpu_get();

    RUNTIME_ASSERT(numa_idx != NULL
                   && local_idx != NULL
                   && cpu_lcore != NULL
                   && hw_numa_id != NULL
                   && hw_cpu_id != NULL);

    *hw_cpu_id = (uint8_t)rte_lcore_id();
    *hw_numa_id = (uint8_t)rte_socket_id();
    *cpu_lcore = (uint8_t)rte_lcore_index(*hw_cpu_id);
    *local_idx = nc->c2n[*cpu_lcore].numa_cpu_id;
    *numa_idx = nc->c2n[*cpu_lcore].numa_id;
}

int dpdk_init(int argc, char *argv[], void *output)
{
    int ret = 0;
    struct hw_info *info = output;

    RUNTIME_ASSERT(argv != NULL);

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure. %s ", rte_strerror(rte_errno));
        return -1;
    }

    rte_srand(rte_rdtsc());

    _dpdk_info(info);
    dpdk_numa_cpu_init(info->numa_count, info->cpu_count);
    dpdk_rcu_create();

    ret = dpdk_port_init();
    if (ret < 0) {
        return -1;
    }

    return 0;
}