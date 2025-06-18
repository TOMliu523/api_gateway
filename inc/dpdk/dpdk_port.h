/************************************************
 * filename: dpdk_port.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_PORT_H__
#define __DPDK_PORT_H__

int dpdk_port_init(void);
int dpdk_port_startup(int port);

#endif // __DPDK_PORT_H__