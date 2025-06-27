/*****************************************************************************
 * filename: l3.c
 * function:
 * description:
 *****************************************************************************/

#include <stdbool.h>

#include "l2.h"
#include "l3.h"
#include "rcu.h"
#include "net.h"
#include "list.h"
#include "type.h"
#include "protocol.h"
#include "dpdk_core.h"
#include "dpdk_common.h"

static int _l3_ip_get_by_port(uint32_t *ip, int port)
{
    struct ipv4_manage *ipv4_manage = rcu_dereference(th_cfg->ipv4_manage);
    struct ipv4_info *info = ipv4_manage->info[port];

    if (info != NULL) {
        *ip = info->ip;
        return 0;
    } else {
        return -1;
    }
}

void l3_refresh(void)
{
    l2_arp_refresh(_l3_ip_get_by_port);
}

bool l3_is_our_ipv4(uint16_t port, uint32_t ip)
{
    uint16_t idx = 0;
    struct ipv4_info *cur = NULL;
    struct list_head *head = NULL;
    struct ipv4_manage *ipv4_manage = rcu_dereference(th_cfg->ipv4_manage);

    idx = L3_IPv4_BUCKET_IDX(ip);
    head = &ipv4_manage->head[idx];

    list_for_each_entry(cur, head, node) {
        if (ipv4_in_subnet(ip, cur->ip, cur->mask)) {
            return true;
        }
    }

    return false;
}

void *l3_thread_ipv4_create(int hw_numa_id)
{
    size_t size = 0;
    struct ipv4_manage *ipv4_manage = NULL;

    size = sizeof(*ipv4_manage) + DPDK_ETHPORT_MAX * sizeof(struct ipv4_info);
    ipv4_manage = dpdk_malloc(size);
    if (UNLIKELY(ipv4_manage == NULL)) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(ipv4_manage, 0, size);

    ipv4_manage->ip_count = 0;
    ipv4_manage->info = (void *)ipv4_manage + sizeof(*ipv4_manage);
    for (int i = 0; i < ARR_NUMS(ipv4_manage->head); i++) {
        INIT_LIST_HEAD(&ipv4_manage->head[i]);
    }

    return ipv4_manage;
}

void l3_thread_ipv4_destroy(void *ptr)
{
    int ip_count = 0;
    struct ipv4_info *cur = NULL;
    struct ipv4_info *next = NULL;
    struct ipv4_manage *ipv4_manage = ptr;

    if (ptr == NULL) {
        return;
    }

    ip_count = ipv4_manage->ip_count;
    for (int i = 0; i < ARR_NUMS(ipv4_manage->head); i++) {
        list_for_each_entry_safe(cur, next, &ipv4_manage->head[i], node) {
            list_del_init(&cur->node);
            dpdk_free(cur);

            if (--ip_count == 0) {
                goto _quit;
            }
        }
    }

_quit:
    dpdk_free(ptr);
    return;
}