/************************************************
 * filename: dataplane.c
 * function:
 * description:
 ***********************************************/

#include <stdio.h>
#include <unistd.h>

#include "log.h"
#include "type.h"
#include "atomic.h"
#include "dpdk_init.h"
#include "dataplane.h"

static __thread struct dataplane dataplane;

static INLINE void _dp_init(void *arg)
{
    struct root *root = arg;
    struct numa_content *numa = NULL;
    struct dataplane *dp = &dataplane;

    dpdk_thread_info(&dp->numa_id, &dp->numa_cpu_id, &dp->dpdk_cpu_id, &dp->hw_numa_id, &dp->hw_cpu_id);
    LOG_INFO("hw_cpu_id = %d, cpu_lcore = %d, local_idx = %d, hw_numa_id = %d, numa_idx = %d",
             dp->hw_cpu_id, dp->dpdk_cpu_id, dp->numa_cpu_id, dp->hw_numa_id, dp->numa_id);

    numa = &root->numa.contents[dp->numa_id];

    dp->nc = &numa->nc;
    numa->dpdk_thread[dp->numa_cpu_id] = dp;
    ATOMIC_ADD_FETCH(&numa->nums, 1);

    dpdk_thread_set_name(dp->numa_id, dp->numa_cpu_id);
}

int dp_startup(void *arg)
{
    // TODO init
    _dp_init(arg);

    for (;;) {
        sleep(1);
    }

    // TODO fini

    return 0;
}