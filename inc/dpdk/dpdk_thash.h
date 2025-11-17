/*****************************************************************************
 * filename: dpdk_thash.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_THASH_H__
#define __DPDK_THASH_H__

#include <rte_thash.h>

#include "macro.h"
#include "dpdk_ip6.h"

#define DPDK_THASH_RETA_SZ_MIN RTE_THASH_RETA_SZ_MIN
#define DPDK_THASH_RETA_SZ_MAX RTE_THASH_RETA_SZ_MAX
#define DPDK_THASH_IGNORE_PERIOD_OVERFLOW RTE_THASH_IGNORE_PERIOD_OVERFLOW
#define DPDK_THASH_MINIMAL_SEQ RTE_THASH_MINIMAL_SEQ

#define dpdk_ip4_tuple rte_ipv4_tuple
#define dpdk_ip6_tuple rte_ipv6_tuple
#define dpdk_thash_tuple rte_thash_tuple

#define dpdk_thash_ctx rte_thash_ctx
#define dpdk_thash_check_tuple_t rte_thash_check_tuple_t
#define dpdk_thash_subtuple_helper rte_thash_subtuple_helper

static INLINE void dpdk_convert_rss_key(const uint32_t *orig, uint32_t *targ, int len)
{
    rte_convert_rss_key(orig, targ, len);
}

static INLINE void dpdk_thash_load6_addrs(const struct dpdk_ip6_hdr *orig, union dpdk_thash_tuple *targ)
{
    rte_thash_load_v6_addrs(orig, targ);
}

static INLINE uint32_t dpdk_softrss(uint32_t *input_tuple, uint32_t input_len, const uint8_t *rss_key)
{
    return rte_softrss(input_tuple, input_len, rss_key);
}

static INLINE uint32_t dpdk_softrss_be(uint32_t *input_tuple, uint32_t input_len, const uint8_t *rss_key)
{
    return rte_softrss_be(input_tuple, input_len, rss_key);
}

static INLINE int dpdk_thash_gfni_supported(void)
{
    return rte_thash_gfni_supported();
}

static INLINE void dpdk_thash_complete_matrix(uint64_t *matrixes, const uint8_t *rss_key, int size)
{
    return rte_thash_complete_matrix(matrixes, rss_key, size);
}

static INLINE struct dpdk_thash_ctx *
dpdk_thash_init_ctx(const char *name, uint32_t key_len,
                    uint32_t reta_sz, uint8_t *key, uint32_t flags)
{
    return rte_thash_init_ctx(name, key_len, reta_sz, key, flags);
}

static INLINE struct dpdk_thash_ctx *dpdk_thash_find_existing(const char *name)
{
    return rte_thash_find_existing(name);
}

static INLINE void dpdk_thash_free_ctx(struct dpdk_thash_ctx *ctx)
{
    return rte_thash_free_ctx(ctx);
}

static INLINE int dpdk_thash_add_helper(struct dpdk_thash_ctx *ctx, const char *name, uint32_t len, uint32_t offset)
{
    return rte_thash_add_helper(ctx, name, len, offset);
}

static INLINE struct dpdk_thash_subtuple_helper *
dpdk_thash_get_helper(struct dpdk_thash_ctx *ctx, const char *name)
{
    return rte_thash_get_helper(ctx, name);
}

static INLINE uint32_t
dpdk_thash_get_complement(struct dpdk_thash_subtuple_helper *h, uint32_t hash, uint32_t desired_hash)
{
    return rte_thash_get_complement(h, hash, desired_hash);
}

static INLINE const uint8_t *dpdk_thash_get_key(struct dpdk_thash_ctx *ctx)
{
    return rte_thash_get_key(ctx);
}

static INLINE const uint64_t *dpdk_thash_get_gfni_matrices(struct dpdk_thash_ctx *ctx)
{
    return rte_thash_get_gfni_matrices(ctx);
}

static INLINE int
dpdk_thash_adjust_tuple(struct dpdk_thash_ctx *ctx, struct dpdk_thash_subtuple_helper *h,
                        uint8_t *tuple, unsigned int tuple_len, uint32_t desired_value,
                        unsigned int attempts, dpdk_thash_check_tuple_t fn, void *userdata)
{
    return rte_thash_adjust_tuple(ctx, h, tuple, tuple_len, desired_value, attempts, fn, userdata);
}

#endif // __DPDK_THASH_H__