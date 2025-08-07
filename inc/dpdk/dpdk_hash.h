/*****************************************************************************
 * filename: dpdk_hash.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_HASH_H__
#define __DPDK_HASH_H__

#include <rte_hash.h>

#include "macro.h"

// type
#define dpdk_hash rte_hash
#define dpdk_hash_function rte_hash_function

#define DPDK_HASH_ENTRIES_MAX RTE_HASH_ENTRIES_MAX
#define DPDK_HASH_NAMESIZE RTE_HASH_NAMESIZE
#define DPDK_HASH_LOOKUP_MAX RTE_HASH_LOOKUP_MULTI_MAX

#define dpdk_hash_cmp_t rte_hash_cmp_eq_t

// create/free/reset/count/set cmp func
extern struct dpdk_hash *dpdk_hash_create(uint32_t, uint32_t, int, dpdk_hash_cmp_t);
extern void dpdk_hash_destroy(struct dpdk_hash *);

static INLINE void dpdk_hash_reset(struct dpdk_hash *hash)
{
    rte_hash_reset(hash);
}

static INLINE int dpdk_hash_count(const struct dpdk_hash *hash)
{
    return rte_hash_count(hash);
}

static INLINE int dpdk_hash_max_key_id(const struct dpdk_hash *hash)
{
    return rte_hash_max_key_id(hash);
}

// add/del
static INLINE int dpdk_hash_add_kv(const struct dpdk_hash *hash, const void *key, void *data)
{
    return rte_hash_add_key_data(hash, key, data);
}

static INLINE int dpdk_hash_add_kv_with_hash(const struct dpdk_hash *hash, const void *key, uint32_t sig, void *data)
{
    return rte_hash_add_key_with_hash_data(hash, key, sig, data);
}

static INLINE int dpdk_hash_add_key(const struct dpdk_hash *hash, const void *key)
{
    return rte_hash_add_key(hash, key);
}

static INLINE int dpdk_hash_add_key_with_hash(const struct dpdk_hash *hash, const void *key, hash_sig_t sig)
{
    return rte_hash_add_key_with_hash(hash, key, sig);
}

static INLINE int dpdk_hash_del_key(const struct dpdk_hash *hash, const void *key)
{
    return rte_hash_del_key(hash, key);
}

static INLINE int dpdk_hash_del_key_with_hash(const struct dpdk_hash *hash, const void *key, hash_sig_t sig)
{
    return rte_hash_del_key_with_hash(hash, key, sig);
}

static INLINE int dpdk_hash_get_key_with_position(const struct dpdk_hash *hash, const int32_t position, void **key)
{
    return rte_hash_get_key_with_position(hash, position, key);
}

static INLINE int dpdk_hash_free_key_with_position(const struct dpdk_hash *hash, const int32_t position)
{
    return rte_hash_free_key_with_position(hash, position);
}

static INLINE int dpdk_hash_lookup(const struct dpdk_hash *hash, const void *key, void **data)
{
    return rte_hash_lookup_data(hash, key, data);
}

static INLINE int dpdk_hash_lookup_bulk(const struct dpdk_hash *hash, const void *keys[], uint32_t num_keys, uint64_t hit_mask[], void *data[])
{
    return rte_hash_lookup_bulk_data(hash, keys, num_keys, hit_mask, data);
}

static INLINE int dpdk_hash_lookup_with_hash_data(const struct dpdk_hash *hash, const void *key, hash_sig_t sig, void **data)
{
    return rte_hash_lookup_with_hash_data(hash, key, sig, data);
}

static INLINE int dpdk_hash_lookup_with_hash(const struct dpdk_hash *hash, const void *key, hash_sig_t sig)
{
    return rte_hash_lookup_with_hash(hash, key, sig);
}

static INLINE hash_sig_t dpdk_hash_hash(const struct dpdk_hash *hash, const void *key)
{
    return rte_hash_hash(hash, key);
}

static INLINE int dpdk_hash_lookup_with_hash_bulk(const struct dpdk_hash *hash, const void *keys[], hash_sig_t *sig, uint32_t num_keys, int32_t *positions)
{
    return rte_hash_lookup_with_hash_bulk(hash, keys, sig, num_keys, positions);
}

#endif // __DPDK_HASH_H__