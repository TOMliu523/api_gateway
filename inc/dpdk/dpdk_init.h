/************************************************
 * filename: dpdk_init.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_INIT_H__
#define __DPDK_INIT_H__

extern int dpdk_init(int argc, char *argv[], void *output);
extern void dpdk_thread_startup(void *f, void *arg);
extern void dpdk_thread_set_name(const char *name);

#endif // __DPDK_INIT_H__