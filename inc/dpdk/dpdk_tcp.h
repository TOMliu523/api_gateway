/*****************************************************************************
 * filename: dpdk_tcp.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __DPDK_TCP_H__
#define __DPDK_TCP_H__

#include <rte_tcp.h>

#define dpdk_tcp_hdr rte_tcp_hdr

#define DPDK_TCP_FIN_F RTE_TCP_FIN_FLAG
#define DPDK_TCP_SYN_F RTE_TCP_SYN_FLAG
#define DPDK_TCP_RST_F RTE_TCP_RST_FLAG
#define DPDK_TCP_PSH_F RTE_TCP_PSH_FLAG
#define DPDK_TCP_ACK_F RTE_TCP_ACK_FLAG
// URG Not Need Support
#define DPDK_TCP_ECE_F RTE_TCP_ECE_FLAG
#define DPDK_TCP_CWR_F RTE_TCP_CWR_FLAG

#endif // __DPDK_TCP_H__