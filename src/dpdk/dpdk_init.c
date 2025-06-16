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

struct numa_cpu {
    int numa_count;
    struct numa_to_cpu {
        int numa_id;
        int hw_numa_id;
        int count;
        int hw_cpu_id[NUMA_CPU_MAX];
        int cpu_lcore[NUMA_CPU_MAX];
    } n2c[NUMA_MAX];

    int cpu_count;
    struct cpu_to_numa {
        int hw_cpu_id;
        int dpdk_cpu_id;
        int numa_cpu_id;
        int dpdk_numa_id;
        int hw_numa_id;
    } c2n[NUMA_MAX * NUMA_CPU_MAX];
};

static struct numa_cpu s_numa_cpu;

static void _dpdk_numa_to_cpu_init(struct numa_cpu *numa_cpu, struct hw_info *info)
{
    int i = 0;
    int count = 0;
    int lcore_id = 0;
    int hw_numa_id = 0;
    int numa[NUMA_MAX] = {0};

    numa_cpu->numa_count = info->numa_count;
    for (i = 0; i < numa_cpu->numa_count; i++) {
        struct numa_to_cpu *n2c = &numa_cpu->n2c[i];

        count = 0;
        n2c->numa_id = i;
        n2c->hw_numa_id = rte_socket_id_by_idx(i);
        RTE_LCORE_FOREACH(lcore_id) {
            hw_numa_id = rte_lcore_to_socket_id(lcore_id);

            RUNTIME_ASSERT(hw_numa_id < NUMA_MAX);

            if (hw_numa_id == n2c->hw_numa_id) {
                n2c->hw_cpu_id[count] = lcore_id;
                n2c->cpu_lcore[count] = rte_lcore_index(lcore_id);

                count += 1;
            }
        }

        n2c->count = count;
    }

    numa_cpu->cpu_count = info->cpu_count;
    RTE_LCORE_FOREACH(lcore_id) {
        int cpu_lcore = rte_lcore_index(lcore_id);
        struct cpu_to_numa *c2n = &numa_cpu->c2n[cpu_lcore];

        c2n->hw_cpu_id = lcore_id;
        c2n->dpdk_cpu_id = cpu_lcore;
        c2n->hw_numa_id = rte_lcore_to_socket_id(lcore_id);
        for (int j = 0; j < NUMA_MAX; j++) {
            if (c2n->hw_numa_id == rte_socket_id_by_idx(j)) {
                c2n->dpdk_numa_id = j;
                break;
            }
        }
    }

    for (i = 0; i < numa_cpu->cpu_count; i++) {
        struct cpu_to_numa *one = &numa_cpu->c2n[i];
        one->numa_cpu_id = numa[one->hw_numa_id]++;
    }
}

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
    _dpdk_numa_to_cpu_init(&s_numa_cpu, output);

    return 0;
}

void dpdk_thread_startup(void *f, void *arg)
{
    RUNTIME_ASSERT(f != NULL);

    LOG_INFO("STARTUP DPDK THREAD.");
    rte_eal_mp_remote_launch(f, arg, CALL_MAIN);
}

void dpdk_thread_set_name(uint8_t numa_idx, uint8_t local_idx)
{
    char name[64] = "";

    snprintf(name, sizeof(name), "DATAPLANE_%X_%02X", numa_idx, local_idx);
    rte_thread_set_name(rte_thread_self(), name);
}

void dpdk_thread_info(uint8_t *numa_idx, uint8_t *local_idx, uint16_t *cpu_lcore, uint16_t *hw_numa_id, uint16_t *hw_cpu_id)
{
    struct numa_cpu *nc = &s_numa_cpu;

    RUNTIME_ASSERT(numa_idx != NULL && local_idx != NULL && cpu_lcore != NULL && hw_numa_id != NULL && hw_cpu_id != NULL);

    *hw_cpu_id = rte_lcore_id();
    *hw_numa_id = rte_socket_id();
    *cpu_lcore = rte_lcore_index(*hw_cpu_id);
    *local_idx = nc->c2n[*cpu_lcore].numa_cpu_id;
    *numa_idx = nc->c2n[*cpu_lcore].dpdk_numa_id;
}