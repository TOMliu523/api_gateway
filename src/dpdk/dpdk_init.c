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

    if (msl == NULL) {
        return 0;
    }

    return dpdk_alloc_socket_get(&info->stat[msl->socket_id], msl->socket_id);
}

int dpdk_alloc_socket_get(void *arg, int socket_id)
{
    int ret = 0;
    struct dpdk_socket_stat *stat = arg;
    struct rte_malloc_socket_stats st = {0};

    ret = rte_malloc_get_socket_stats(socket_id, &st);
    if (ret < 0) {
        LOG_ERROR("Function(rte_malloc_get_socket_stats) failure: %s", rte_strerror(ret));
        return -1;
    }

    stat->heap_totalsz_bytes = st.heap_totalsz_bytes;
    stat->heap_freesz_bytes = st.heap_freesz_bytes;
    stat->greatest_free_size = st.greatest_free_size;
    stat->heap_allocsz_bytes = st.heap_allocsz_bytes;
    stat->free_count = st.free_count;
    stat->alloc_count = st.alloc_count;

    return 0;
}

void dpdk_hw_info_init(void *arg)
{
    struct hw_info *info = (struct hw_info *)arg;

    info->cpu_count = rte_lcore_count();
    info->nic_count = rte_eth_dev_count_avail();
    info->numa_count = dpdk_numa_cpu_init(info->cpu_count);
    rte_memseg_walk(_dpdk_memory_info, info);

    LOG_INFO("cpu_count = %d, nic_count = %d, numa_count = %d", info->cpu_count, info->nic_count, info->numa_count);
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

int dpdk_init(int argc, char *argv[])
{
    int ret = 0;

    RUNTIME_ASSERT(argv != NULL);

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure: %s", rte_strerror(rte_errno));
        return -1;
    }

    rte_srand(rte_rdtsc());
    return ret;
}

/*
int dpdk_init(int argc, char *argv[])
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
    info->numa_count = dpdk_numa_cpu_init(info->cpu_count);

    dpdk_rcu_create();

    ret = dpdk_port_init();
    if (ret < 0) {
        return -1;
    }

    return 0;
}
*/

void dpdk_fini(int signo)
{
    LOG_ERROR("Receive signal no %d", signo);
    rte_eal_cleanup();
    exit(EXIT_FAILURE);
}