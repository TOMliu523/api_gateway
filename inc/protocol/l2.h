/************************************************
 * filename: l2.h
 * function:
 * description:
 ***********************************************/

#ifndef __L2_H__
#define __L2_H__

#include "dpdk_type.h"

extern int l2_garp_gen(struct dpdk_mbuf *mbuf, uint32_t addr, struct dpdk_mac *mac);

#endif // __L2_H__