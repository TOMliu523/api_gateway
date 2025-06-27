/*****************************************************************************
 * filename: dpdk_hash.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_HASH_H__
#define __DPDK_HASH_H__

#include <rte_hash.h>

// type
#define dpdk_hash rte_hash
#define dpdk_hash_param rte_hash_parameters

// create/free/reset/count/set cmp func
#define dpdk_hash_create rte_hash_create
#define dpdk_hash_set_cmp_func rte_hash_set_cmp_func
#define dpdk_hash_free rte_hash_free
#define dpdk_hash_reset rte_hash_reset
#define dpdk_hash_count rte_hash_count

// max key id
#define dpdk_hash_max_key_id rte_hash_max_key_id

// add/del
#define dpdk_hash_add_key rte_hash_add_key
#define dpdk_hash_del_key rte_hash_del_key
#define dpdk_hash_add_kv rte_hash_add_key_data

// add/del with hash
#define dpdk_hash_add_key_with_hash rte_hash_add_key_with_hash
#define dpdk_hash_del_key_with_hash rte_hash_del_key_with_hash
#define dpdk_hash_add_kv_with_hash rte_hash_add_key_with_hash_data

// get/free with position
#define dpdk_hash_get_key_with_position rte_hash_get_key_with_position
#define dpdk_hash_free_key_with_position rte_hash_free_key_with_position

// lookup
#define dpdk_hash_lookup_kv rte_hash_lookup_data
#define dpdk_hash_lookup_with_hash rte_hash_lookup_with_hash
#define dpdk_hash_lookup_with_hash_data rte_hash_lookup_with_hash_data
#define dpdk_hash_lookup rte_hash_lookup
// lookup bulk
#define dpdk_hash_lookup_bulk_kv rte_hash_lookup_bulk_data
#define dpdk_hash_lookup_bulk_with_hash rte_hash_lookup_with_hash_bulk

// calc a hash value by key
#define dpdk_hash_hash rte_hash_hash

// rcu
#define dpdk_hash_rcu_thread_add rte_hash_rcu_qsbr_add
#define dpdk_hash_rcu_delete rte_hash_rcu_qsbr_dq_reclaim

#endif // __DPDK_HASH_H__