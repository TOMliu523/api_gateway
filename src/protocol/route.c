/*****************************************************************************
 * filename: route.c
 * function:
 * description:
 ****************************************************************************/

#include <arpa/inet.h>

#include "l3.h"
#include "log.h"
#include "type.h"
#include "route.h"
#include "errcode.h"
#include "dpdk_common.h"
#include "dpdk_spinlock.h"

#define L3_DIRECT_ROUTE_ITEM_MAX 2000
#define L3_ROUTE_ITEM_MAX 20000
#define L3_ROUTE_DEFAULT_INVALID_ID (UINT32_MAX)

// Route table structure definition
struct route_table {
    // LPM lookup structure using DPDK's rte_lpm for longest prefix match
	struct dpdk_fib *fib;
    // Default route (-1 invalid)
    uint32_t default_id;
    // Total number of active route entries (may include non-direct routes)
	int store_count;
    // Array storing all route entries (up to 20,000 entries)
    // Lookup is done via LPM, access is via index into this array
	struct route_item store[L3_ROUTE_ITEM_MAX];
};

struct route_ctl {
    int numa_count;
    struct dpdk_lpm *numa_route[NUMA_MAX];
};

static struct route_ctl s_route_ctl;
static __thread struct route_table *tls_route = NULL;
// Lock protecting concurrent writes by config thread & per-NUMA threads
// Read operations are lockless
static dpdk_spinlock_t s_route_conf_spinlock;

static INLINE bool _route_conf_is_broadcast_ip(uint32_t local_ip_be, uint8_t mask, uint32_t next_hop_be)
{
    uint32_t local_ip = dpdk_be_to_cpu_32(local_ip_be);
    uint32_t next_hop = dpdk_be_to_cpu_32(next_hop_be);
    uint32_t mask_ip = L3_MASK_TO_IP(mask);

    return next_hop == ((local_ip & mask_ip) | ~mask_ip);
}

static int _route_conf_create(void **out, int hw_numa_id)
{
    char name[CACHE_LINE] = "";
    struct route_table *route = NULL;

    static uint64_t s_route_version[DPDK_ETHPORT_MAX] = {0};

    route = dpdk_malloc_numa(sizeof(*route), hw_numa_id);
    if (UNLIKELY(route == NULL)) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    memset(route, 0, sizeof(*route));

    route->default_id = L3_ROUTE_DEFAULT_INVALID_ID;

    snprintf(name, sizeof(name), "ROUTE_%d_%lu", hw_numa_id, s_route_version[hw_numa_id]++);
    route->fib = dpdk_fib_create(name, hw_numa_id, L3_ROUTE_ITEM_MAX);
    if (UNLIKELY(route->fib == NULL)) {
        dpdk_free(route);
        return ERRCODE_OOM;
    }

    *out = route;
    return 0;
}

static void _route_conf_destroy(void *arg)
{
    struct route_table *route = arg;

    if (route != NULL) {
        if (route->fib != NULL) {
            dpdk_fib_destroy(route->fib);
        }

        dpdk_free(route);
    }
}

static int _route_conf_add_item(struct route_table *route, const struct route_item *item)
{
    int ret = 0;
    uint64_t next_hop = 0;
    uint32_t dst_subnet = 0;
    struct route_item *store = NULL;

    store = &route->store[route->store_count];
    *store = *item;
    INIT_LIST_HEAD(&store->lru_head);

    if (store->route_type == ROUTE_DIRECT) {
        store->direct_id = 0;
    } else {
        ret = dpdk_fib_lookup(route->fib, (uint32_t *)&item->nexthop, &next_hop, 1);
        if (LIKEYLY(ret == 0 && next_hop != DPDK_FIB_DEFAULT)) {
            struct route_item *next_hop_item = &route->store[next_hop];
            if (next_hop_item->route_type == ROUTE_DIRECT) {
                store->direct_id = next_hop;
            } else {
                store->direct_id = next_hop_item->direct_id;
            }
        }
    }

    dst_subnet = dpdk_be_to_cpu_32(item->dst_subnet);
    ret = dpdk_fib_add(route->fib, dst_subnet, item->mask, (uint64_t)route->store_count);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Fib add error.");
        return ERRCODE_INNER;
    }

    if (item->dst_subnet == 0 && item->mask == 0) {
        route->default_id = route->store_count;
    }

    route->store_count += 1;
    return 0;
}

static int _route_conf_add_check(struct route_table *route, const struct route_item *item, int count, const void *arg)
{
    int ret = 0;
    uint8_t mask1 = 0;
    uint8_t mask2 = 0;
    uint64_t next_hop = 0;
    uint32_t host_subnet1 = 0;
    uint32_t host_subnet2 = 0;
    char ip_str[CACHE_LINE] = "";
    struct route_item *next_item = NULL;
    struct route_item *direct_item = NULL;

    // unique prefix
    for (int i = 0; i < count; i++) {
        host_subnet1 = dpdk_be_to_cpu_32(item[i].dst_subnet);
        mask1 = item[i].mask;

        for (int j = 0; j < count; j++) {
            if (i == j) {
                continue;
            }

            host_subnet2 = dpdk_be_to_cpu_32(item[j].dst_subnet);
            mask2 = item[j].mask;

            if (mask1 == mask2 && (host_subnet1 & L3_MASK_TO_IP(mask1)) == (host_subnet2 & L3_MASK_TO_IP(mask2))) {
                LOG_ERROR("Cannot have two routing entries with the same subnet.");
                return ERRCODE_ROUTE_CONFLICT;
            }
        }

        for (int j = 0; route != NULL && j < route->store_count; j++) {
            host_subnet2 = dpdk_be_to_cpu_32(route->store[j].dst_subnet);
            mask2 = route->store[j].mask;

            if (mask1 == mask2 && (host_subnet1 & L3_MASK_TO_IP(mask1)) == (host_subnet2 & L3_MASK_TO_IP(mask2))) {
                LOG_ERROR("Cannot have two routing entries with the same subnet.");
                return ERRCODE_ROUTE_CONFLICT;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        const struct route_item *one = &item[i];
        if (one->route_type == ROUTE_DIRECT) {
            continue;
        }

        /*
         * Valid next hop
         * Check for the existence of a directly connected route.
         */
        do {
            if (route == NULL) {
                break;
            }

            ret = dpdk_fib_lookup(route->fib, (uint32_t *)&one->nexthop, &next_hop, 1);
            if (UNLIKELY(ret == 0 && next_hop == DPDK_FIB_DEFAULT) || (ret != 0)) {
                inet_ntop(AF_INET, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("No reachable link exists, the route next hop(%#x) is invalid.", ip_str);
                return ERRCODE_ROUTE_NEXTHOP_UNREACHABLE;
            }
        } while (0);

        /*
         * Ensure the uniqueness of the default route.
         */
        do {
            uint8_t mask = one->mask;
            uint32_t ip = one->dst_subnet;

            if (route == NULL) {
                break;
            }

            if (UNLIKELY(ip == 0 && mask == 0 && route->default_id != L3_ROUTE_DEFAULT_INVALID_ID)) {
                LOG_ERROR("Multiple default routes are not allowed.");
                return ERRCODE_ROUTE_MULTI_DEFAULT;
            }
        } while (0);

        /*
         * The address is neither a broadcast address nor a multicast address.
         */
        do {
            uint32_t nexthop = dpdk_be_to_cpu_32(one->nexthop);

            // check multicast address
            if (*(uint8_t *)&nexthop >= 0xE0 && *(uint8_t *)&nexthop <= 0xEF) {
                inet_ntop(AF_INET, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("Nexthop(%s) is multicast", ip_str);
                return ERRCODE_ROUTE_NEXTHOP_INVALID;
            }

            if (route == NULL) {
                break;
            }

            // check broadcast address
            next_item = &route->store[(uint16_t)next_hop];
            if (next_item->route_type == ROUTE_DIRECT) {
                direct_item = next_item;
            } else {
                direct_item = &route->store[next_item->direct_id];
            }

            if (_route_conf_is_broadcast_ip(direct_item->dst_subnet, direct_item->mask, one->nexthop)) {
                inet_ntop(AF_INET, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("Nexthop(%s) is broadcasts.", ip_str);
                return ERRCODE_ROUTE_NEXTHOP_INVALID;
            }
        } while (0);

        /*
         * No local IP references
         */
        do {
            if (l3_conf_ipv4_manage_ip_is_local(arg, one->nexthop, one->interface)) {
                inet_ntop(AF_INET, &one->nexthop, ip_str, sizeof(ip_str));
                LOG_ERROR("The next hop is a local IP(%s) address.", ip_str);
                return ERRCODE_ROUTE_LOCAL_IP;
            }
        } while (0);
    }

    return 0;
}

static int _route_conf_del_check(const struct route_table *route, const struct route_item *item, int count, bool is_route)

{
    bool hit = false;
    char ip_str[CACHE_LINE] = "";
    const struct route_item *store = NULL;

    for (int i = 0, j = 0; i < count; i++) {
        hit = false;
        store = &item[i];

        if (is_route) {
            for (j = 0; j < route->store_count; j++) {
                if (store->dst_subnet == route->store[j].dst_subnet && store->mask == route->store[j].mask) {
                    hit = true;
                    break;
                }
            }
        } else {
            for (j = 0; j < route->store_count; j++) {
                if (store->dst_subnet == route->store[j].dst_subnet && store->interface == route->store[j].interface) {
                    hit = true;
                    break;
                }
            }
        }

        if (!hit) {
            inet_ntop(AF_INET, &store->dst_subnet, ip_str, sizeof(ip_str));
            LOG_ERROR("Such a subnet(%s) and netmask(%d) combination does not exist.", ip_str, store->interface);
            return ERRCODE_SUBNET_NO_EXIST;
        }

        if (route->store[j].route_type == ROUTE_DIRECT) {
            continue;
        }

        for (int m = 0; m < route->store_count; m++) {
            if (route->store[m].route_type != ROUTE_DIRECT && route->store[m].direct_id == j) {
                LOG_ERROR("A route cannot be deleted if it is referenced.");
                return ERRCODE_ROUTE_REFERENCED;
            }
        }
    }

    return 0;
}

static int _route_conf_append(struct route_table *dst, struct route_table *route, const struct route_item *item, int count)
{
    int ret = 0;

    for (int i = 0; route != NULL && i < route->store_count; i++) {
        ret = _route_conf_add_item(dst, &route->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _route_conf_add_item(dst, &item[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _route_conf_delete(struct route_table *dst, struct route_table *src, const struct route_item *item, int count, bool is_route)
{
    bool hit = false;
    struct route_item *one = NULL;

    for (int i = 0; i < src->store_count; i++) {
        hit = false;
        one = &src->store[i];

        if (is_route) {
            for (int j = 0; j < count; j++) {
                if (one->dst_subnet == item[j].dst_subnet && one->mask == item[j].mask) {
                    hit = true;
                    break;
                }
            }
        } else {
            for (int j = 0; j < count; j++) {
                if (one->dst_subnet == item[j].dst_subnet && one->interface == item[j].interface) {
                    hit = true;
                    break;
                }
            }
        }

        if (!hit) {
            _route_conf_add_item(dst, one);
        }
    }

    return 0;
}

void route_conf_destroy(void *ptr)
{
    _route_conf_destroy(ptr);
}

int route_conf_create_and_append(void **dst, void *src, const struct route_item *item, int count, int hw_numa_id, const void *arg)
{
    int ret = 0;
    struct route_table *route = src;

    ret = _route_conf_add_check(route, item, count, arg);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route_conf_create(dst, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route_conf_append(*dst, route, item, count);
    if (UNLIKELY(ret != 0)) {
        _route_conf_destroy(*dst);
        *dst = NULL;
        return ret;
    }

    return 0;
}

int route_conf_create_and_delete(void **dst, void *src, const struct route_item *items, int count, int hw_numa_id, bool is_route)
{
    int ret = 0;
    struct route_table *route = src;

    ret = _route_conf_del_check(route, items, count, is_route);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route_conf_create(dst, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _route_conf_delete(*dst, route, items, count, is_route);
    if (UNLIKELY(ret != 0)) {
        _route_conf_destroy(*dst);
        return ret;
    }

    return 0;
}

void route_conf_table_get(void *src, struct route_item **item, int *count)
{
    struct route_table *route = src;

    if (src == NULL) {
        return;
    }

    *item = route->store;
    *count = route->store_count;
}

void route_conf_update_lock(void)
{
    dpdk_spinlock_lock(&s_route_conf_spinlock);
}

void route_conf_update_unlock(void)
{
    dpdk_spinlock_unlock(&s_route_conf_spinlock);
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface