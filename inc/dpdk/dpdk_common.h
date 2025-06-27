/************************************************
 * filename: dpdk_common.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_COMMON_H__
#define __DPDK_COMMON_H__

#include <rte_malloc.h>

#include "macro.h"

/**********************************************************************/
/***************************** MEMORY *********************************/
/**********************************************************************/
static INLINE void *dpdk_malloc(size_t size)
{
    return rte_malloc(NULL, size, CACHE_LINE);
}

static INLINE void *dpdk_zmalloc(size_t size)
{
    return rte_zmalloc(NULL, size, CACHE_LINE);
}

static INLINE void *dpdk_calloc(size_t num, size_t size)
{
    return rte_calloc(NULL, num, size, CACHE_LINE);
}

static INLINE void *dpdk_realloc(void *ptr, size_t size)
{
    return rte_realloc(ptr, size, CACHE_LINE);
}

static INLINE void *dpdk_malloc_numa(size_t size, int numa)
{
    return rte_malloc_socket(NULL, size, CACHE_LINE, numa);
}

static INLINE void *dpdk_zmalloc_numa(size_t size, int numa)
{
    return rte_zmalloc_socket(NULL, size, CACHE_LINE, numa);
}

static INLINE void *dpdk_calloc_numa(size_t num, size_t size, int numa)
{
    return rte_calloc_socket(NULL, num, size, CACHE_LINE, numa);
}

static INLINE void dpdk_free(void *ptr)
{
    if (ptr != NULL) {
        rte_free(ptr);
    }
}

static INLINE int dpdk_malloc_size(const void *ptr, size_t *size)
{
    return rte_malloc_validate(ptr, size);
}

/**********************************************************************/
/****************************** TIMER *********************************/
/**********************************************************************/
static INLINE uint64_t dpdk_timer_cycles(void)
{
    return rte_get_timer_cycles();
}

static INLINE uint64_t dpdk_timer_hz(void)
{
    return rte_get_timer_hz();
}

static INLINE void *dpdk_memcpy(void *dst, const void *src, size_t n)
{
    return rte_memcpy(dst, src, n);
}

#endif // __DPDK_COMMON_H__