/*****************************************************************************
 * filename: dpdk_spinlock.h
 * function:
 * description:
 ****************************************************************************/

#include <rte_spinlock.h>

#include "macro.h"

#define dpdk_spinlock_t rte_spinlock_t

static INLINE void dpdk_spinlock_init(dpdk_spinlock_t *spinlock)
{
    rte_spinlock_init(spinlock);
}

static INLINE void dpdk_spinlock_lock(dpdk_spinlock_t *spinlock)
{
    rte_spinlock_lock_tm(spinlock);
}

static INLINE void dpdk_spinlock_unlock(dpdk_spinlock_t *spinlock)
{
    rte_spinlock_unlock_tm(spinlock);
}

static INLINE unsigned int dpdk_spinlock_trylock(dpdk_spinlock_t *spinlock)
{
    return rte_spinlock_trylock_tm(spinlock);
}