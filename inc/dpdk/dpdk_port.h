/*****************************************************************************
 * filename: dpdk_port.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_PORT_H__
#define __DPDK_PORT_H__

#include <stdint.h>
#include <stdbool.h>

#include <rte_ethdev.h>

#include "macro.h"
#include "dpdk_type.h"

#define DPDK_PORT_MTU 1500
#define DPDK_ETH_NAME_LEN_MAX RTE_ETH_NAME_MAX_LEN

struct port_info_entry {
    uint8_t port;
    char name[DPDK_ETH_NAME_LEN_MAX];
    char pci[DPDK_ETH_NAME_LEN_MAX];
};

struct port_info {
    int count;
    struct port_info_entry info[DPDK_ETHPORT_MAX];
};

extern int dpdk_port_init(void);
extern bool dpdk_port_is_up(int port);
extern int dpdk_port_restart(int port);
extern int dpdk_port_stop(int port);
extern const struct port_info *dpdk_port_info_get(void);
extern uint8_t dpdk_port_by_name_get(const char *name);
extern const char *dpdk_port_id_to_name(int port);

extern uint64_t dpdk_port_rx_offload_get(int nic_number);
extern uint64_t dpdk_port_tx_offload_get(int nic_number);

static INLINE int dpdk_port_mac(uint16_t port, struct dpdk_mac *mac)
{
    return rte_eth_macaddr_get(port, mac);
}

static INLINE bool dpdk_port_is_valid(uint16_t port)
{
    return rte_eth_dev_is_valid_port(port);
}

static INLINE int dpdk_port_set_mtu(uint16_t port, uint16_t mtu)
{
    return rte_eth_dev_set_mtu(port, mtu);
}

#endif // __DPDK_PORT_H__