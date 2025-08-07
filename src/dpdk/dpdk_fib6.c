/*****************************************************************************
 * filename: dpdk_fib6.h
 * function:
 * description:
 ****************************************************************************/

#include <string.h>

#include <rte_errno.h>

#include "log.h"
#include "atomic.h"
#include "dpdk_fib6.h"

struct dpdk_fib6 *dpdk_fib6_create(int hw_numa_id, int max_item)
{
    int ret = 0;
    char name[32] = "";
    struct dpdk_fib6 *fib = NULL;
    struct rte_fib6_conf conf = {
        .type = RTE_FIB6_TRIE,
        .default_nh = DPDK_FIB6_DEFAULT,
        .max_routes = max_item,
        .rib_ext_sz = 0,
        .trie = {
            .nh_sz = RTE_FIB6_TRIE_4B,
            .num_tbl8 = max_item / 3,
        },
    };

    static uint32_t s_seq = 0;

    atomic_fetch_add(&s_seq, 1);
    snprintf(name, sizeof(name), "DPDK_FIB6_%u_%u", hw_numa_id, s_seq);

    fib = rte_fib6_create(name, hw_numa_id, &conf);
    if (fib == NULL) {
        LOG_ERROR("Failure rte_fib6_create: %s", strerror(rte_errno));
        return NULL;
    }

    ret = rte_fib6_select_lookup(fib, RTE_FIB6_LOOKUP_DEFAULT);
    if (ret != 0) {
        LOG_ERROR("Failure rte_fib6_select_lookup: %s", strerror(-rte_errno));
        rte_fib6_free(fib);
        return NULL;
    }

    return fib;
}

void dpdk_fib6_destroy(struct dpdk_fib6 *fib)
{
    if (fib != NULL) {
        rte_fib6_free(fib);
    }
}