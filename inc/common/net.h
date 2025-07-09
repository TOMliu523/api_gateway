 /*****************************************************************************
 * filename: net.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __NET_H__
#define __NET_H__

#include <stdbool.h>

#include "macro.h"
#include "dpdk_type.h"

static INLINE bool ipv4_in_subnet(uint32_t ip_be, uint32_t subnet_be, uint32_t netmask)
{
    uint32_t ip = dpdk_be_to_cpu_32(ip_be);
    uint32_t subnet = dpdk_be_to_cpu_32(subnet_be);

    return (ip & netmask) == (subnet & netmask);
}

#endif // __NET_H__