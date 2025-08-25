/*****************************************************************************
 * filename: dpdk_timer.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_TIMER_H__
#define __DPDK_TIMER_H__

#include <rte_lcore.h>
#include <rte_timer.h>

#include "macro.h"

#define dpdk_timer_type rte_timer_type
#define DPDK_SINGLE SINGLE
#define DPDK_PERIODICAL PERIODICAL
#define dpdk_timer_cb_t rte_timer_cb_t

#define dpdk_timer timeout

extern int dpdk_timer_start(void);
extern void dpdk_timer_close(void);
extern void *dpdk_timer_pool_get(int numa_id);
extern void *dpdk_timer_thread_create(void);
extern void dpdk_timer_thread_destroy(void *);

static INLINE int dpdk_timer_pop(void *pool, void *data[], int max)
{
    return dpdk_mempool_pop(pool, (void **)data, max);
}

static INLINE void dpdk_timer_push(void *pool, void *data[], int max)
{
    dpdk_mempool_push(pool, (void **)data, max);
}

static INLINE void dpdk_timer_init(struct dpdk_timer *timer)
{
    timeout_init(timer, TIMEOUT_ABS);
}

static INLINE void dpdk_timer_add(void *handle, struct dpdk_timer *timer, timeout_t cur)
{
    timeouts_add(handle, timer, cur);
}

static INLINE void dpdk_timer_del(void *handle, struct dpdk_timer *timer)
{
    timeouts_del(handle, timer);
}

static INLINE void dpdk_timer_trigger(void *handle, int limits, timeout_t cur, void *pool, void *objs[])
{
    timeouts_update(handle, cur);
    timeouts_trigger(handle, limits, dpdk_timer_pop, pool, objs);
}

#endif // __DPDK_TIMER_H__