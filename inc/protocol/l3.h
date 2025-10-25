/*****************************************************************************
 * filename: l3.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __L3_H__
#define __L3_H__

#include <limits.h>
#include <stdbool.h>

#include "list.h"
#include "dpdk_ip6.h"

#define L3_INTERFACE_INVALID UINT8_MAX
#define L3_MASK_TO_IP(m) ((m) == 0 ? 0 : ((~0U) << (32 - (m))))

enum IP_LOCAL_CLASS {
    IP_LOCAL_CLASS_EXTERNAL,
    IP_LOCAL_CLASS_SELF,
    IP_LOCAL_CLASS_LAN,
};

enum IP_TYPE {
    IP_MASTER,
    IP_SECONDARY,
};

struct ip4_info {
    struct list_head node;
    uint32_t addr;
    uint16_t port;
    uint8_t mask;
    uint8_t type; // enum IP_TYPE
    uint32_t refcnt;
};

struct ip6_info {
    struct dpdk_ip6_addr addr;
    uint16_t port;
    uint8_t mask;
    uint8_t type;
    uint32_t refcnt;
};

#endif // __L3_H__
