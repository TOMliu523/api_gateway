/************************************************
 * filename: dpdk_init.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INIT_H__
#define __DPDK_INIT_H__

extern int dpdk_init(int argc, char *argv[]);
extern void dpdk_thread_startup(void *f, void *arg);

#endif // __DPDK_INIT_H__