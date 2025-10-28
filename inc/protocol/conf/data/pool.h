/*****************************************************************************
 * filename: pool.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __POOL_H__
#define __POOL_H__

#include <stdint.h>

#include "rserver.h"

#define POOL_ID_INVALID (-1)

enum RS_SELECT_ALGO {
    RS_ALGO_RR = 0,
    RS_ALGO_RANDOM,
    RS_ALGO_WRR,
    // TODO
    RS_ALGO_MAX
};

struct rserver_rr {
    int rs_count;
    uint32_t next;
    uint32_t ids[];
};

struct rserver_random {
    int rs_count;
    uint32_t ids[];
};

struct pool {
    /*
     * void * points to either struct rserver_rr
     * or struct rserver_random, depending on type.
     */
    uint32_t (*pool_rs_get_next)(void *);
    union {
        struct rserver_rr *rr;
        struct rserver_random *random;

        void *ptr;
    };

    /*
     * Management structure — used only by the control plane.
     * It does not affect or participate in the data plane.
     */
    struct {
        uint32_t id;
        enum RS_SELECT_ALGO type;
        uint32_t refcnt;
        // The name must always be the last entry; queried only by the configuration plane
        char name[];
    };
};

extern void *pool_thread_create(void ***, int);
extern void pool_thread_destroy(void *);

static INLINE void pool_refcnt_inc(struct pool *pool)
{
    pool->refcnt += 1;
}

static INLINE void pool_refcnt_dec(struct pool *pool)
{
    pool->refcnt -= 1;
}

#endif // __POOL_H__