/*****************************************************************************
 * filename: route6.c
 * function:
 * description: High-Performance Longest Prefix Match Library
 ****************************************************************************/

#include "ip6.h"
#include "log.h"
#include "errcode.h"
#include "ip6_conf.h"
#include "dpdk_fib6.h"
#include "dpdk_common.h"
#include "route6_conf.h"

#define ROUTE6_DIRECT_ITEM_MAX 2000
#define ROUTE6_ITEM_MAX 20000
#define ROUTE6_DEFAULT_INVALID_ID (UINT32_MAX)

struct route6_table {
    struct dpdk_fib6 *fib;
    uint32_t default_id;
    int store_count;
    struct route6_item store[ROUTE6_ITEM_MAX];
};

static int _route6_conf_add_check(struct route6_table *route6, const struct route6_item *item, int count, const void *arg)
{
    int ret = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";

    for (int i = 0; i < count; i++) {
        const struct route6_item *cur = NULL;
        const struct route6_item *next = NULL;

        cur = &item[i];
        for (int j = 0; j < count; j++) {
            if (i == j) {
                continue;
            }

            next = &item[j];
            if (UNLIKELY(dpdk_ip6_addr_eq(&cur->dst_subnet, &next->dst_subnet) && cur->mask == next->mask)) {
                LOG_ERROR("Cannot have two routing entries with the same subnet.");
                return ERRCODE_ROUTE_CONFLICT;
            }
        }

        if (route6 == NULL) {
            continue;
        }

        for (int j = 0; j < route6->store_count; j++) {
            next = &route6->store[j];
            if (UNLIKELY(dpdk_ip6_addr_eq(&cur->dst_subnet, &next->dst_subnet) && cur->mask == next->mask)) {
                LOG_ERROR("Cannot have two routing entries with the same subnet.");
                return ERRCODE_ROUTE_CONFLICT;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        const struct route6_item *one = &item[i];
        if (one->route_type == ROUTE6_DIRECT) {
            continue;
        }

        /*
         * Valid next hop
         * Check for the existence of a directly connected route.
         */
        do {
            if (route6 == NULL) {
                break;
            }

            ret = dpdk_fib6_lookup(route6->fib, &one->nexthop, &next_hop, 1);
            if (UNLIKELY(ret != 0 || (ret == 0 && next_hop == DPDK_FIB6_DEFAULT))) {
                inet_ntop(AF_INET6, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("No reachable link exists, the route next hop(%#x) is invalid.", ip_str);
                return ERRCODE_ROUTE_NEXTHOP_UNREACHABLE;
            }
        } while (0);

        /*
         * Ensure the uniqueness of the default route.
         */
        do {
            if (route6 == NULL) {
                break;
            }

            if (UNLIKELY(dpdk_ip6_addr_is_unspec(&one->dst_subnet) && one->mask == 0
                && route6->default_id != ROUTE6_DEFAULT_INVALID_ID)) {
                LOG_ERROR("Multiple default routes are not allowed.");
                return ERRCODE_ROUTE_MULTI_DEFAULT;
            }
        } while (0);

        /*
         * The address is neither a broadcast address nor a multicast address.
         */
        do {
            // check multicast address
            if (UNLIKELY(*(uint8_t *)&one->nexthop == 0xFF)) {
                inet_ntop(AF_INET6, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("Nexthop(%s) is multicast", ip_str);
                return ERRCODE_ROUTE_NEXTHOP_INVALID;
            }
        } while (0);

        /*
         * No local IP references
         */
        do {
            if (UNLIKELY(ip6_conf_manage_ip_is_local(arg, &one->nexthop, one->interface))) {
                inet_ntop(AF_INET6, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("The next hop is a local IP(%s) address.", ip_str);
                return ERRCODE_ROUTE_LOCAL_IP;
            }
        } while (0);
    }

    return 0;
}

static int _route6_conf_del_check(struct route6_table *route6, const struct route6_item *item, int count, bool is_route)
{
    bool hit = false;
    char ip_str[CACHE_LINE] = "";
    const struct route6_item *one = NULL;
    const struct route6_item *store = NULL;

    for (int i = 0, j = 0; i < count; i++) {
        hit = false;
        one = &item[i];

        if (is_route) {
            for (j = 0; j < route6->store_count; j++) {
                store = &route6->store[j];
                if (dpdk_ip6_addr_eq(&one->dst_subnet, &store->dst_subnet) && one->mask == store->mask) {
                    hit = true;
                    break;
                }
            }
        } else {
            for (j = 0; j < route6->store_count; j++) {
                store = &route6->store[j];
                if (dpdk_ip6_addr_eq(&one->dst_subnet, &store->dst_subnet) && one->interface == store->interface) {
                    hit = true;
                    break;
                }
            }
        }

        if (!hit) {
            inet_ntop(AF_INET6, &one->dst_subnet, ip_str, sizeof(ip_str));
            LOG_ERROR("Such a subnet(%s) and netmask(%d) combination does not exists.", ip_str, one->mask);
            return ERRCODE_SUBNET_NO_EXIST;
        }

        if (route6->store[j].route_type == ROUTE6_DIRECT) {
            continue;
        }

        for (int m = 0; m < route6->store_count; m++) {
            if (route6->store[m].route_type != ROUTE6_DIRECT && route6->store[m].direct_id == j) {
                LOG_ERROR("A route cannot be deleted if it is referenced.");
                return ERRCODE_ROUTE_REFERENCED;
            }
        }
    }

    return 0;
}

static int _route6_conf_create(void **dst, int hw_numa_id)
{
    struct route6_table *route6 = NULL;

    route6 = dpdk_malloc_numa(sizeof(*route6), hw_numa_id);
    if (UNLIKELY(route6 == NULL)) {
        LOG_ERROR("OOM");
        return ERRCODE_OOM;
    }

    memset(route6, 0, sizeof(*route6));

    route6->default_id = DPDK_FIB6_DEFAULT;
    route6->fib = dpdk_fib6_create(hw_numa_id, ROUTE6_ITEM_MAX);
    if (UNLIKELY(route6->fib == NULL)) {
        return ERRCODE_OOM;
    }

    *dst = route6;
    return 0;
}

static int _route6_conf_add_item(struct route6_table *route6, const struct route6_item *item)
{
    int ret = 0;
    uint64_t next_hop = 0;
    struct route6_item *store = NULL;

    store = &route6->store[route6->store_count];
    *store = *item;
    INIT_LIST_HEAD(&store->lru_head);

    if (store->route_type == ROUTE6_DIRECT) {
        store->direct_id = 0;
    } else {
        ret = dpdk_fib6_lookup(route6->fib, &store->dst_subnet, &next_hop, 1);
        if (UNLIKELY(ret == 0 && next_hop != DPDK_FIB6_DEFAULT)) {
            struct route6_item *next_hop_item = &route6->store[next_hop];
            if (next_hop_item->route_type == ROUTE6_DIRECT) {
                store->direct_id = next_hop;
            } else {
                store->direct_id = next_hop_item->direct_id;
            }
        }
    }

    ret = dpdk_fib6_add(route6->fib, &store->dst_subnet, store->mask, (uint64_t)route6->store_count);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Fib6 add error.");
        return ERRCODE_INNER;
    }

    if (dpdk_ip6_addr_is_unspec(&store->dst_subnet) && item->mask == 0) {
        route6->default_id = route6->store_count;
    }

    route6->store_count += 1;
    return 0;
}

static int _route6_conf_append(void *dst, struct route6_table *route, const struct route6_item *item, int count)
{
    int ret = 0;

    for (int i = 0; route != NULL && i < route->store_count; i++) {
        ret = _route6_conf_add_item(dst, &route->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _route6_conf_add_item(dst, &item[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _route6_conf_delete(void *dst, struct route6_table *src, const struct route6_item *item, int count, bool is_route)
{
    bool hit = false;
    struct route6_item *one = NULL;

    for (int i = 0; i < src->store_count; i++) {
        hit = false;
        one = &src->store[i];

        if (is_route) {
            for (int j = 0; j < count; j++) {
                if (dpdk_ip6_addr_eq(&one->dst_subnet, &item[j].dst_subnet) && one->mask == item[j].mask) {
                    hit = true;
                    break;
                }
            }
        } else {
            for (int j = 0; j < count; j++) {
                if (dpdk_ip6_addr_eq(&one->dst_subnet, &item[j].dst_subnet) && one->interface == item[j].interface) {
                    hit = true;
                    break;
                }
            }
        }

        if (!hit) {
            _route6_conf_add_item(dst, one);
        }
    }

    return 0;
}

int route6_conf_create_and_append(void **dst, void *src, const struct route6_item *item, int count, int hw_numa_id, const void *arg)
{
    int ret = 0;
    struct route6_table *route6 = src;

    ret = _route6_conf_add_check(route6, item, count, arg);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route6_conf_create(dst, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route6_conf_append(*dst, route6, item, count);
    if (UNLIKELY(ret != 0)) {
        route6_conf_destroy(*dst);
        *dst = NULL;
        return ret;
    }

    return 0;
}

int route6_conf_create_and_delete(void **dst, void *src, const struct route6_item *item, int count, int hw_numa_id, bool is_route)
{
    int ret = 0;
    struct route6_table *route6 = src;

    ret = _route6_conf_del_check(route6, item, count, is_route);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route6_conf_create(dst, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route6_conf_delete(*dst, route6, item, count, is_route);
    if (UNLIKELY(ret != 0)) {
        route6_conf_destroy(*dst);
        *dst = NULL;
        return ret;
    }

    return 0;
}

void route6_conf_table_get(void *src, struct route6_item **item, int *count)
{
    struct route6_table *route6 = src;

    if (src == NULL) {
        return;
    }

    *item = route6->store;
    *count = route6->store_count;

    return;
}

void route6_conf_destroy(void *ptr)
{
    struct route6_table *route6 = ptr;

    if (route6 != NULL) {
        if (route6->fib != NULL) {
            dpdk_fib6_destroy(route6->fib);
        }

        dpdk_free(route6);
    }
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface