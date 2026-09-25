/*****************************************************************************
 * filename: dpdk_rcu.h
 * function:
 * description:
 *              The RCU (Read-Copy-Update) library provides APIs for
 *              managing shared data in a safe, efficient,
 *              and scalable manner without locking readers.
 ****************************************************************************/

#include <string.h>

#include <rte_rcu_qsbr.h>

#include "macro.h"
#include "atomic.h"

#define dpdk_rcu rte_rcu_qsbr
#define dpdk_rcu_dq rte_rcu_qsbr_dq
#define dpdk_rcu_dq_param rte_rcu_qsbr_dq_parameters

#ifdef __x86_64
#define rcu_dereference(p) \
    ({ \
        typeof(p) __p = p; \
        atomic_prevent_compiler_reorder(); \
        __p; \
    })
#elif defined(__aarch64__)
#define rcu_dereference(p) atomic_load_explicit(&(p), memory_order_consume);
#else
#error "Unsupported architecture"
#endif

#ifndef rcu_assign_pointer
#define rcu_assign_pointer(p, v) \
	do { \
		__typeof__(*p) __pv = (v); \
		atomic_store_explicit(p, __pv, \
            __builtin_constant_p(v) && (v) == NULL ? memory_order_relaxed : memory_order_release);	\
	} while (0)
#endif // rcu_assign_pointer

// RCU
extern int dpdk_rcu_create(void);
extern void dpdk_rcu_destroy(void);
extern struct dpdk_rcu *dpdk_rcu_get(int cpu_id);

static INLINE int dpdk_rcu_thread_register(struct dpdk_rcu *v, unsigned int thread_id)
{
    return rte_rcu_qsbr_thread_register(v, thread_id);
}

static INLINE int dpdk_rcu_thread_unregister(struct dpdk_rcu *v, unsigned int thread_id)
{
    return rte_rcu_qsbr_thread_unregister(v, thread_id);
}

static INLINE void dpdk_rcu_thread_online(struct dpdk_rcu *v, unsigned int thread_id)
{
    return rte_rcu_qsbr_thread_online(v, thread_id);
}

static INLINE void dpdk_rcu_thread_offline(struct dpdk_rcu *v, unsigned int thread_id)
{
    return rte_rcu_qsbr_thread_offline(v, thread_id);
}

static INLINE void dpdk_rcu_lock(UNUSED struct dpdk_rcu *v, UNUSED unsigned int thread_id)
{
    rte_rcu_qsbr_lock(v, thread_id);
}

static INLINE void dpdk_rcu_unlock(UNUSED struct dpdk_rcu *v, UNUSED unsigned int thread_id)
{
    rte_rcu_qsbr_unlock(v, thread_id);
}

static INLINE void dpdk_rcu_quiescent(struct dpdk_rcu *v, unsigned int thread_id)
{
    rte_rcu_qsbr_quiescent(v, thread_id);
}

static INLINE void dpdk_rcu_synchronize(struct dpdk_rcu *v)
{
    rte_rcu_qsbr_synchronize(v, RTE_QSBR_THRID_INVALID);
}

static INLINE int dpdk_rcu_dump(FILE *f, struct dpdk_rcu *v)
{
    return rte_rcu_qsbr_dump(f, v);
}

// Defer Queue
static INLINE struct dpdk_rcu_dq *dpdk_rcu_dq_create(const struct dpdk_rcu_dq_param *param)
{
    return rte_rcu_qsbr_dq_create(param);
}

static INLINE int dpdk_rcu_dq_delete(struct dpdk_rcu_dq *dq)
{
    return rte_rcu_qsbr_dq_delete(dq);
}

static INLINE int dpdk_rcu_dq_enqueue(struct dpdk_rcu_dq *dq, void *e)
{
    return rte_rcu_qsbr_dq_enqueue(dq, e);
}

static INLINE int dpdk_rcu_dq_reclaim(struct dpdk_rcu_dq *dq, uint32_t n, uint32_t *freed, uint32_t *pending, uint32_t *avail)
{
    return rte_rcu_qsbr_dq_reclaim(dq, n, freed, pending, avail);
}