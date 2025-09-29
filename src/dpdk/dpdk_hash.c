/*****************************************************************************
 * filename: dpdk_hash.c
 * function:
 * description:
 ****************************************************************************/

#include <time.h>
#include <string.h>

#include <rte_errno.h>

#include "log.h"
#include "atomic.h"
#include "errcode.h"
#include "dpdk_crc.h"
#include "dpdk_hash.h"

#define dpdk_hash_param rte_hash_parameters

struct dpdk_hash *dpdk_hash_create(uint32_t max_entries, uint32_t key_len, int hw_numa_id, dpdk_hash_cmp_t cmp)
{
    uint32_t seq = 0;
    struct timespec spec = {0};
    struct dpdk_hash *hash = NULL;
    char name[DPDK_HASH_NAMESIZE] = "";
    struct dpdk_hash_param param = {
        .name = name,
        .entries = max_entries,
        .reserved = 0,
        .key_len = key_len,
        .hash_func = dpdk_hash_crc_with_init_val,
        .hash_func_init_val = 0,
        .socket_id = hw_numa_id,
        .extra_flag = RTE_HASH_EXTRA_FLAGS_EXT_TABLE,
    };

    static uint32_t s_seq = 0;

    /*
     * Avoid failures caused by leftover DPDK hugepage objects during rapid restarts,
     * where stale memzone allocations may still exist and cause object name conflicts.
     */
    seq = atomic_fetch_add(&s_seq, 1);
    clock_gettime(CLOCK_MONOTONIC, &spec);
    snprintf(name, sizeof(name), "HASH_%lu_%u_%u", spec.tv_sec, hw_numa_id, seq);
    LOG_ERROR("%s", name);

    hash = rte_hash_create(&param);
    if (hash == NULL) {
        LOG_ERROR("Hash error: %s", strerror(rte_errno));
        return NULL;
    }

    rte_hash_set_cmp_func(hash, cmp);

    return hash;
}

void dpdk_hash_destroy(struct dpdk_hash *hash)
{
    return rte_hash_free(hash);
}