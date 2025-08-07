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
#include "type.h"

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

struct ipv4_info {
    struct list_head node;
    uint32_t ip;
    uint16_t port;
    uint8_t mask;
    uint8_t type; // enum IP_TYPE
};

struct ipv6_info {
    union dpdk_ipv6_addr ipv6;
    uint16_t port;
    uint8_t mask;
    uint8_t type;
};

extern int l3_conf_ipv4_manage_create_and_append(void **dst,
                                                 void *src,
                                                 const struct ipv4_info *info,
                                                 int count,
                                                 int hw_numa_id);
extern int l3_conf_ipv4_manage_create_and_delete(void **dst,
                                                 void *src,
                                                 const struct ipv4_info *info,
                                                 int count,
                                                 int hw_numa_id);
extern bool l3_conf_ipv4_manage_ip_is_local(const void *arg, uint32_t ip, uint8_t port);

extern int l3_conf_ipv6_manage_create_and_append(void **dst,
                                                 void *src,
                                                 const struct ipv6_info *info,
                                                 int count,
                                                 int hw_numa_id);
extern int l3_conf_ipv6_manage_create_and_delete(void **dst,
                                                 void *src,
                                                 const struct ipv6_info *info,
                                                 int count,
                                                 int hw_numa_id);
extern bool l3_conf_ipv6_manage_ip_is_local(const void *arg, const union dpdk_ipv6_addr *addr, uint8_t port);
// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

extern void l3_process(void);

extern void l3_refresh(void);
extern void l3_ipv4_manage_destroy(void *);

extern void *l3_thread_startup(int, int);

/**
 * @brief Lightweight dataplane API to classify a local IPv4 address.
 *
 * This version is optimized for high-speed dataplane usage, avoiding
 * any locking or logging overhead. It should be called only from
 * data path threads where state is already up-to-date and safe.
 *
 * @param port The port ID (physical or logical interface).
 * @param ip   The IPv4 address in network byte order.
 * @return One of the IP_LOCAL_CLASS enum values:
 *         - IP_LOCAL_CLASS_OTHER: Not local and not in same subnet.
 *         - IP_LOCAL_CLASS_SELF: The IP is assigned to the local interface.
 *         - IP_LOCAL_CLASS_LAN: The IP is within the same subnet but not local.
 */
extern enum IP_LOCAL_CLASS l3_ipv4_local_class(uint16_t port, uint32_t ip);

#endif // __L3_H__
