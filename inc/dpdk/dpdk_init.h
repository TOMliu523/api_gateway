/************************************************
 * filename: dpdk_init.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INIT_H__
#define __DPDK_INIT_H__

#include <stdint.h>

#include <rte_memcpy.h>

extern void dpdk_fini(int signo);
extern void dpdk_hw_info_init(void *arg);
extern int dpdk_init(int argc, char *argv[]);
extern void dpdk_thread_startup(void *f, void *arg);
extern void dpdk_thread_set_name(uint8_t numa_idx, uint8_t cpu_id);
extern int dpdk_alloc_socket_get(void *arg, int socket_id);
extern void dpdk_thread_info(uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);

#endif // __DPDK_INIT_H__