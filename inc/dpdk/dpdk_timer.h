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

#define dpdk_timer rte_timer

extern int dpdk_timer_startup(void);
extern void *dpdk_timer_pool_get(int);
extern void dpdk_timer_shutdown(void);

static INLINE int dpdk_timer_subsystem_init(void)
{
    return rte_timer_subsystem_init();
}

static INLINE void dpdk_timer_subsystem_fini(void)
{
    return rte_timer_subsystem_finalize();
}

static INLINE void dpdk_timer_init(struct dpdk_timer *timer)
{
    return rte_timer_init(timer);
}

static INLINE int dpdk_timer_stop(struct dpdk_timer *timer)
{
    return rte_timer_stop(timer);
}

static INLINE int dpdk_timer_pending(struct dpdk_timer *timer)
{
    return rte_timer_pending(timer);
}

static INLINE int dpdk_timer_trigger(void)
{
    return rte_timer_manage();
}

static INLINE int dpdk_timer_start_once(struct dpdk_timer *timer, uint64_t ticks, dpdk_timer_cb_t fn, void *arg)
{
    return rte_timer_reset(timer, ticks, DPDK_SINGLE, rte_lcore_id(), fn, arg);
}

static INLINE int dpdk_timer_start_repeat(struct dpdk_timer *timer, uint64_t ticks, dpdk_timer_cb_t fn, void *arg)
{
    return rte_timer_reset(timer, ticks, DPDK_PERIODICAL, rte_lcore_id(), fn, arg);
}

static INLINE int dpdk_timer_pop(void *pool, struct dpdk_timer *data[], int max)
{
    return dpdk_mempool_pop(pool, (void **)data, max);
}

static INLINE int dpdk_timer_push(void *pool, struct dpdk_timer *data[], int max)
{
    dpdk_mempool_push(pool, (void **)data, max);
}

#endif // __DPDK_TIMER_H__