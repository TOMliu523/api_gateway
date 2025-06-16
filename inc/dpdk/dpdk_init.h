/************************************************
 * filename: dpdk_init.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INIT_H__
#define __DPDK_INIT_H__

#include <stdint.h>

#include "macro.h"

extern int dpdk_init(int argc, char *argv[], void *output);
extern void dpdk_thread_startup(void *f, void *arg);
extern void dpdk_thread_set_name(uint8_t numa_idx, uint8_t local_idx);
extern void dpdk_thread_info(uint8_t *, uint8_t *, uint16_t *, uint16_t *, uint16_t *);

#endif // __DPDK_INIT_H__