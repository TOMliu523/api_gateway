/*****************************************************************************
 * filename: rcu.h
 * function:
 * description: This set of interfaces does not allow nesting.
 ****************************************************************************/

#ifndef __RCU_H__
#define __RCU_H__

#include "type.h"
#include "macro.h"
#include "atomic.h"

static INLINE void rcu_read_lock(struct dataplane *dp)
{
    atomic_prevent_compiler_reorder();
    uint64_t global_version = atomic_load_relaxed(&dp->tc->version);
    atomic_store_relaxed(&dp->version, global_version);
    atomic_barrier();
}

static INLINE void rcu_read_unlock(struct dataplane *dp)
{
    atomic_barrier();
}

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

static INLINE void rcu_synchronize(struct dataplane *dps[], int nums)
{
    for (int i = 0; i < nums; i++) {
        uint64_t version = atomic_load_relaxed(&dps[i]->tc->version);
        atomic_store_relaxed(&dps[i]->tc->version, version + 1);
    }

    atomic_barrier();

    for (int i = 0; i < nums; i++) {
        struct dataplane *dp = dps[i];
        while (atomic_load_relaxed(&dp->version) != atomic_load_relaxed(&dp->tc->version)
               && atomic_load_relaxed(&dp->version) != 0) {
            PAUSE();
        }
    }

    atomic_barrier();
}

#endif // __RCU_H__