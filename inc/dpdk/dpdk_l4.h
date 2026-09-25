/*****************************************************************************
 * filename: dpdk_l4.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_L4_H__
#define __DPDK_L4_H__

#include <rte_mbuf_core.h>

#define DPDK_RX_L4_CKSUM_GOOD RTE_MBUF_F_RX_L4_CKSUM_GOOD
#define DPDK_RX_L4_CKSUM_BAD RTE_MBUF_F_RX_L4_CKSUM_BAD
#endif // __DPDK_L4_H__