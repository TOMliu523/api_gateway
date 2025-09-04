/************************************************
 * filename: dpdk_core.h
 * function:
 * description: ring mbuf pool
 ***********************************************/

#ifndef __DPDK_CORE_H__
#define __DPDK_CORE_H__

#include "log.h"
#include "dpdk_type.h"

#define dpdk_ring rte_ring
#define dpdk_pool rte_mempool

#define DPDK_POOL_CACHE_SIZE 64
#define dpdk_append(mbuf, len, t) ((t) rte_pktmbuf_append(mbuf, len))
#define dpdk_prepend(mbuf, len, t) ((t) rte_pktmbuf_prepend(mbuf, len))

extern int dpdk_pool_pktmbuf_create(void);
extern void * const *dpdk_pool_pktmbuf_get(void);
extern void *dpdk_pool_pktmbuf_get_by_numa(int);
extern void *dpdk_indirect_pool_pktmbuf_get_by_numa(int numa_id);

/**********************************************************************/
/****************************** POOL **********************************/
/**********************************************************************/
static INLINE void *dpdk_pool_create(const char *name, unsigned n, unsigned elt_size, int socket, unsigned flag)
{
    void *ptr = NULL;

    ptr = rte_mempool_create(name, n, elt_size, DPDK_POOL_CACHE_SIZE, 0, NULL, NULL, NULL, NULL, socket, flag);
    if (ptr == NULL) {
        LOG_ERROR("Failure name(%s) rte_mempool_create: %s", name, strerror(rte_errno));
        return NULL;
    }

    return ptr;
}

static INLINE void *dpdk_pool_ss_create(const char *name, unsigned n, unsigned elt_size, int socket)
{
    return dpdk_pool_create(name, n, elt_size, socket, RTE_MEMPOOL_F_SP_PUT | RTE_MEMPOOL_F_SC_GET);
}

static INLINE void *dpdk_pool_sm_create(const char *name, unsigned n, unsigned elt_size, int socket)
{
    return dpdk_pool_create(name, n, elt_size, socket, RTE_MEMPOOL_F_SP_PUT);
}

static INLINE void *dpdk_pool_ms_create(const char *name, unsigned n, unsigned elt_size, int socket)
{
    return dpdk_pool_create(name, n, elt_size, socket, RTE_MEMPOOL_F_SC_GET);
}

static INLINE void *dpdk_pool_mm_create(const char *name, unsigned n, unsigned elt_size, int socket)
{
    return dpdk_pool_create(name, n, elt_size, socket, 0);
}

static INLINE void dpdk_pool_destroy(void *ptr)
{
    rte_mempool_free(ptr);
}

/**********************************************************************/
/****************************** RING **********************************/
/**********************************************************************/
static INLINE void *dpdk_ring_create(const char *name, unsigned count, int socket, int flag)
{
    void *ptr = NULL;

    ptr = rte_ring_create(name, count, socket, flag);
    if (ptr == NULL) {
        LOG_ERROR("Failure name(%s) rte_ring_create: %s", name, strerror(rte_errno));
        return NULL;
    }

    return ptr;
}

static INLINE void *dpdk_ring_ss_create(const char *name, unsigned count, int socket)
{
    return dpdk_ring_create(name, count, socket, RING_F_SP_ENQ | RING_F_SC_DEQ);
}

static INLINE void *dpdk_ring_sm_create(const char *name, unsigned count, int socket)
{
    return dpdk_ring_create(name, count, socket, RING_F_SP_ENQ | RING_F_MC_HTS_DEQ);
}

static INLINE void *dpdk_ring_ms_create(const char *name, unsigned count, int socket)
{
    return dpdk_ring_create(name, count, socket, RING_F_MP_HTS_ENQ | RING_F_SC_DEQ);
}

static INLINE void *dpdk_ring_mm_create(const char *name, unsigned count, int socket)
{
    return dpdk_ring_create(name, count, socket, RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);
}

static INLINE void dpdk_ring_destroy(void *ptr)
{
    if (ptr != NULL) {
        rte_ring_free(ptr);
    }
}

static INLINE unsigned int dpdk_ring_mp_push(struct dpdk_ring *r, void *const objs[], unsigned int n)
{
    return rte_ring_mp_enqueue_burst(r, objs, n, NULL);
}

static INLINE unsigned int dpdk_ring_sp_push(struct dpdk_ring *r, void *const objs[], unsigned int n)
{
    return rte_ring_sp_enqueue_burst(r, objs, n, NULL);
}

static INLINE unsigned int dpdk_ring_push(struct dpdk_ring *r, void *const objs[], unsigned int n)
{
    return rte_ring_enqueue_burst(r, objs, n, NULL);
}

static INLINE unsigned int dpdk_ring_mc_pop(struct dpdk_ring *r, void **objs, unsigned int n)
{
    return rte_ring_mc_dequeue_burst(r, objs, n, NULL);
}

static INLINE unsigned int dpdk_ring_sc_pop(struct dpdk_ring *r, void **objs, unsigned int n)
{
    return rte_ring_sc_dequeue_burst(r, objs, n, NULL);
}

static INLINE unsigned int dpdk_ring_pop(struct dpdk_ring *r, void **objs, unsigned int n)
{
    return rte_ring_dequeue_burst(r, objs, n, NULL);
}

/**********************************************************************/
/****************************** PKTMBUF *******************************/
/**********************************************************************/
static INLINE int dpdk_pktmbuf_pop(struct dpdk_pool *pool, void *mbufs[], unsigned count)
{
    return rte_pktmbuf_alloc_bulk(pool, (struct dpdk_mbuf **)mbufs, count);
}

static INLINE void dpdk_pktmbuf_push(void *mbufs[], unsigned count)
{
    rte_pktmbuf_free_bulk((struct dpdk_mbuf **)mbufs, count);
}

static INLINE struct dpdk_mbuf *dpdk_pktmbuf_copy(const struct dpdk_mbuf *src_mbuf, struct dpdk_pool *pool)
{
    return rte_pktmbuf_copy(src_mbuf, pool, 0, src_mbuf->pkt_len);
}

static INLINE struct dpdk_mbuf *dpdk_pktmbuf_clone(struct dpdk_mbuf *src_mbuf, struct dpdk_pool *pool)
{
    return rte_pktmbuf_clone(src_mbuf, pool);
}

static INLINE int dpdk_pktmbuf_rx(uint16_t port, uint16_t queue, void *mbufs[], const uint16_t max)
{
    return rte_eth_rx_burst(port, queue, (struct dpdk_mbuf **)mbufs, max);
}

static INLINE int dpdk_pktmbuf_tx(uint16_t port, uint16_t queue, void *mbufs[], uint16_t max)
{
    return rte_eth_tx_burst(port, queue, (struct dpdk_mbuf **)mbufs, max);
}

static INLINE char *dpdk_pktmbuf_adj(struct dpdk_mbuf *mbuf, size_t len)
{
    return rte_pktmbuf_adj(mbuf, len);
}

static INLINE void *dpdk_pktmbuf_prepend(struct dpdk_mbuf *mbuf, size_t len)
{
    return rte_pktmbuf_prepend(mbuf, len);
}

static INLINE struct dpdk_arp_hdr *dpdk_pktmbuf_arp(struct dpdk_mbuf *m)
{
    return rte_pktmbuf_mtod_offset(m, struct dpdk_arp_hdr *, sizeof(struct dpdk_eth_hdr));
}

static INLINE const void *dpdk_pktmbuf_read(const struct dpdk_mbuf *mbuf, uint32_t off, uint32_t len, void *buf)
{
    return rte_pktmbuf_read(mbuf, off, len, buf);
}

static INLINE int dpdk_pktmbuf_trim(struct dpdk_mbuf *mbuf, uint16_t len)
{
    return rte_pktmbuf_trim(mbuf, len);
}

static INLINE bool dpdk_pktmbuf_is_continue(const struct dpdk_mbuf *mbuf)
{
    return rte_pktmbuf_is_contiguous(mbuf);
}

#define DPDK_PKTMBUF_TO_TYPE(m, type, offset) rte_pktmbuf_mtod_offset(m, type, offset)

/**********************************************************************/
/****************************** MBUF **********************************/
/**********************************************************************/
static INLINE int dpdk_mempool_pop(struct dpdk_pool *pool, void *obj[], unsigned int n)
{
    return rte_mempool_get_bulk(pool, obj, n);
}

static INLINE void dpdk_mempool_push(struct dpdk_pool *pool, void *const obj, unsigned int n)
{
    rte_mempool_put_bulk(pool, obj, n);
}

static INLINE uint16_t dpdk_mbuf_refcnt_read(const struct dpdk_mbuf *m)
{
    return rte_mbuf_refcnt_read(m);
}

static INLINE void dpdk_mbuf_refcnt_set(struct dpdk_mbuf *m, uint16_t new_value)
{
    return rte_mbuf_refcnt_set(m, new_value);
}

static INLINE uint16_t dpdk_mbuf_refcnt_update(struct dpdk_mbuf *m, int16_t value)
{
    return rte_mbuf_refcnt_update(m, value);
}

/**********************************************************************/
/****************************** COUNT *********************************/
/**********************************************************************/
static INLINE unsigned int dpdk_mempool_avail_count(const struct dpdk_pool *pool)
{
    return rte_mempool_avail_count(pool);
}

static INLINE unsigned int dpdk_mempool_used_count(const struct dpdk_pool *pool)
{
    return rte_mempool_in_use_count(pool);
}

#endif // __DPDK_CORE_H__