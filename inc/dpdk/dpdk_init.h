/************************************************
 * filename: dpdk_init.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INIT_H__
#define __DPDK_INIT_H__

#include <stdint.h>

#include <rte_memcpy.h>

#include "macro.h"

extern int dpdk_init(int argc, char *argv[], void *output);
extern void dpdk_thread_startup(void *f, void *arg);
extern void dpdk_thread_set_name(uint8_t numa_idx, uint16_t dpdk_cpu_id);
extern void dpdk_thread_info(uint8_t *, uint8_t *, uint16_t *, uint16_t *, uint16_t *);

static INLINE void *dpdk_memcpy(void *dst, const void *src, size_t n)
{
    return rte_memcpy(dst, src, n);
}

#endif // __DPDK_INIT_H__