/************************************************
 * filename: dpdk_port.h
 * function:
 * description:
 ***********************************************/

#ifndef __DPDK_PORT_H__
#define __DPDK_PORT_H__

#include <stdint.h>

#include <rte_ethdev.h>

#include "macro.h"
#include "dpdk_type.h"

#define DPDK_ETH_NAME_LEN_MAX RTE_ETH_NAME_MAX_LEN
#define DPDK_ETHPORT_MAX RTE_MAX_ETHPORTS

struct port_name_entry {
    uint8_t port;
    char name[DPDK_ETH_NAME_LEN_MAX];
    char pci[DPDK_ETH_NAME_LEN_MAX];
};

struct port_name {
    int count;
    struct port_name_entry entrys[DPDK_ETHPORT_MAX];
};

extern int dpdk_port_init(void);
extern int dpdk_port_startup(int port);
extern int dpdk_port_restart(int port);
extern int dpdk_port_stop(int port);
extern const struct port_name *dpdk_port_name_get(void);
extern uint16_t dpdk_port_by_name_get(const char *name);

static INLINE int dpdk_port_mac(uint16_t port, struct dpdk_mac *mac)
{
    return rte_eth_macaddr_get(port, mac);
}

#endif // __DPDK_PORT_H__