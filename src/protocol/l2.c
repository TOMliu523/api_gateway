 /*****************************************************************************
 * filename: l2.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "log.h"
#include "dpdk_core.h"

int l2_garp_gen(struct dpdk_mbuf *mbuf, uint32_t addr, struct dpdk_mac *mac)
{
    struct dpdk_eth *eth = NULL;
    struct dpdk_arp *arp = NULL;
    struct rte_ether_addr broadcast = {
        .addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };

    eth = dpdk_append(mbuf, sizeof(*eth) + sizeof(*arp), struct dpdk_eth *);
    if (UNLIKELY(eth == NULL)) {
        LOG_ERROR("There is not enough tailroom space in the last segment.");
        return -1;
    }

    eth->dst_addr = broadcast;
    eth->src_addr = *mac;
    eth->ether_type = dpdk_to_be_16(DPDK_ETHER_ARP);

    arp = (struct dpdk_arp *)(eth + 1);
    arp->arp_hardware = dpdk_to_be_16(1);
    arp->arp_protocol = dpdk_to_be_16(DPDK_ETHER_IPV4);
    arp->arp_hlen = 6;
    arp->arp_plen = 4;
    arp->arp_opcode = dpdk_to_be_16(1);

    // arp_data
    arp->arp_data.arp_sha = *mac;
    arp->arp_data.arp_sip = addr;
    arp->arp_data.arp_tha = broadcast;
    arp->arp_data.arp_tip = addr;

    return 0;
}
