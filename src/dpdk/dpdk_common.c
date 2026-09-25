/************************************************
 * filename: dpdk_common.c
 * function:
 * description:
 ***********************************************/

#include <rte_lcore.h>

#include "log.h"
#include "dpdk_inner.h"

static struct numa_cpu s_numa_cpu;

int dpdk_cpu_count_get(void)
{
    return s_numa_cpu.cpu_count;
}

int dpdk_numa_count_get(void)
{
    return s_numa_cpu.numa_count;
}

struct numa_cpu *dpdk_numa_cpu_get(void)
{
    return &s_numa_cpu;
}

int dpdk_numa_cpu_init(int cpu_count)
{
    int lcore_id = 0;
    int numa_count = 0;
    int numa[NUMA_MAX] = {0};
    int numa_id[NUMA_MAX] = {0};
    struct numa_cpu *nc = &s_numa_cpu;

    if (nc->inited) {
        return nc->numa_count;
    }

    nc->cpu_count = cpu_count;
    RTE_LCORE_FOREACH(lcore_id) {
        int cpu_lcore = rte_lcore_index(lcore_id);
        struct cpu_to_numa *c2n = &nc->c2n[cpu_lcore];

        c2n->hw_cpu_id = lcore_id;
        c2n->cpu_id = cpu_lcore;
        c2n->hw_numa_id = rte_lcore_to_socket_id(lcore_id);
        c2n->numa_cpu_id = numa[c2n->hw_numa_id]++;
    }

    for (int i = 0; i < NUMA_MAX; i++) {
        if (numa[i] != 0) {
            numa_id[i] = numa_count++;
        }
    }

    for (int i = 0; i < cpu_count; i++) {
        struct cpu_to_numa *c2n = &nc->c2n[i];
        c2n->numa_id = numa_id[c2n->hw_numa_id];
    }

    nc->numa_count = numa_count;
    for (int i = 0; i < numa_count; i++) {
        int count = 0;
        int hw_numa_id = 0;
        struct numa_to_cpu *n2c = &nc->n2c[i];

        n2c->numa_id = i;
        n2c->hw_numa_id = rte_socket_id_by_idx(i);
        RTE_LCORE_FOREACH(lcore_id) {
            hw_numa_id = rte_lcore_to_socket_id(lcore_id);

            if (hw_numa_id == n2c->hw_numa_id) {
                n2c->hw_cpu_id[count] = lcore_id;
                n2c->cpu_id[count] = rte_lcore_index(lcore_id);

                count += 1;
            }
        }

        n2c->count = count;
    }

    nc->inited = 1;
    return numa_count;
}