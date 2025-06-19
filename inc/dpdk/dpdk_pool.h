/************************************************
 * filename: dpdk_pool.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_POOL_H__
#define __DPDK_POOL_H__

extern int dpdk_pool_pktmbuf_init(void);
extern void * const *dpdk_pool_pktmbuf_get(void);

#endif // __DPDK_POOL_H__