/************************************************
 * filename: dpdk_type.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_TYPE_H__
#define __DPDK_TYPE_H__

#include <rte_ethdev.h>

#define dpdk_mac rte_ether_addr
#define dpdk_eth_info rte_eth_dev_info
#define dpdk_eth_conf rte_eth_conf

#endif // __DPDK_TYPE_H__