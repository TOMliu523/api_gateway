/*****************************************************************************
 * filename: dpdk_fib.c
 * function:
 * description: High-Performance Longest Prefix Match Library
 ****************************************************************************/

#include <time.h>

#include "log.h"
#include "atomic.h"
#include "dpdk_fib.h"
#include "rte_errno.h"

#define DPDK_FIB_ITEM_MIN 8

struct dpdk_fib *dpdk_fib_create(int hw_numa_id, int max_item)
{
    uint32_t seq = 0;
    struct timespec spec = {0};
    char name[CACHE_LINE] = "";
    int n_item = (max_item < DPDK_FIB_ITEM_MIN) ? DPDK_FIB_ITEM_MIN : max_item;
    struct rte_fib_conf conf = {
        .type = RTE_FIB_DIR24_8,
        .default_nh = DPDK_FIB_DEFAULT,
        .max_routes = n_item,
        .rib_ext_sz = 0,
        .dir24_8 = {
            .nh_sz = RTE_FIB_DIR24_8_4B,
            .num_tbl8 = n_item / 3,
        },
        /**
         * If this flag is set, rte_fib_lookup() expects IPv4 addresses in
         * network byte order (big-endian).
         *
         * Note: Regardless of this flag, rte_fib_add(), rte_fib_delete(), and
         * rte_fib_modify() always expect IPv4 addresses in host byte order.
         *
         * In summary:
         * - Use host byte order for rte_fib_add()
         * - Use network byte order for rte_fib_lookup() if this flag is enabled
         */
        .flags = RTE_FIB_ALLOWED_FLAGS,
    };

    int ret = 0;
    struct dpdk_fib *fib = NULL;

    static uint32_t s_seq = 0;

    seq = atomic_fetch_add(&s_seq, 1);
    clock_gettime(CLOCK_MONOTONIC, &spec);
    snprintf(name, sizeof(name), "FIB_%lu_%u_%u", spec.tv_sec, hw_numa_id, seq);

    fib = rte_fib_create(name, hw_numa_id, &conf);
    if (fib == NULL) {
        LOG_ERROR("Failure rte_fib_create: %s", rte_strerror(rte_errno));
        return NULL;
    }

    ret = rte_fib_select_lookup(fib, RTE_FIB_LOOKUP_DEFAULT);
    if (ret != 0) {
        LOG_ERROR("Failure rte_fib_select_lookup: %s", rte_strerror(rte_errno));
        rte_fib_free(fib);
        return NULL;
    }

    return fib;
}