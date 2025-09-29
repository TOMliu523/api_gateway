/*****************************************************************************
 * filename: ip4.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

#include <stdint.h>

#include "l3.h"

extern void ip4_process(void *[], int);
extern void ip4_manage_destroy(void *);
extern void *ip4_manage_startup(int, int);

extern void ip4_arp_refresh(void);

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
extern enum IP_LOCAL_CLASS ip4_local_class(uint16_t port, uint32_t ip);