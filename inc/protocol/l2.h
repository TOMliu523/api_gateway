/************************************************
 * filename: l2.h
 * function:
 * description:
 ***********************************************/

#ifndef __L2_H__
#define __L2_H__

#include "list.h"
#include "dpdk_port.h"

#define MAC_ADDR_MASK 0xFFFFFFFFFFFFUL

#define MAC_IS_MULTICAST(p) (((uint8_t *)(p))[0] & 1)
#define MAC_IS_TO_LOCAL(p) (((uint8_t *)(p))[0] & 1)

#if defined(__ORDER_LITTLE_ENDIAN__)
#define MAC_ADDR_CMP(p1, p2) ((*(uint64_t *)(p1) & MAC_ADDR_MASK) == ((*(uint64_t *)(p2)) & MAC_ADDR_MASK))
#define MAC_IS_BROADCASTS(p) ((*(uint64_t *)(p) & MAC_ADDR_MASK) == MAC_ADDR_MASK)
#else
#define MAC_ADDR_CMP(p1, p2) ((*(uint64_t *)(p1) >> 16) == (*(uint64_t *)(p2) >> 16))
#define MAC_IS_BROADCASTS(p) ((*(uint64_t *)(p) >> 16) == MAC_ADDR_MASK)
#endif // __ORDER_LITTLE_ENDIAN__

struct iface {
    int nums;
    uint16_t port[];
};

extern int l2_port_mac(uint16_t port, struct dpdk_mac *mac);

extern void *l2_thread_mac_create(int);
extern void l2_thread_mac_destroy(void *);
extern void l2_thread_port_mac(uint16_t port, struct dpdk_mac *mac);

extern void l2_arp_parse(void);
extern void l2_arp_refresh(int (*func)(uint32_t *, uint32_t, int));
extern void l2_arp_update_or_create(void *arg);

extern void l2_thread_arp_table_destroy(void *);
extern void *l2_thread_arp_table_create(int, int, int);
extern int l2_arp_mac_get(struct dpdk_mac *mac, int port, uint32_t be_ip);
extern int l2_gratuitous_arp_gen(struct dpdk_mbuf *mbuf, uint16_t port, uint32_t addr, struct dpdk_mac *mac);

extern void l2_process(void **data, int count);

#endif // __L2_H__