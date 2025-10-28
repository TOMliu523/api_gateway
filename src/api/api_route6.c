/*****************************************************************************
 * filename: api_route6.c
 * function:
 * description:
 *****************************************************************************/

#include <arpa/inet.h>

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_common.h"
#include "route6_conf.h"

#define API_ROUTE6_MODULE_NAME "route6"
#define API_ROUTE6_LIST_NAME "entries"

static INLINE void _api_route6_item_free(struct route6_item *item)
{
    if (item != NULL) {
        dpdk_free(item);
    }
}

static INLINE void _api_route6_table_free(void *route[], int count)
{
    for (int i = 0; i < count; i++) {
        if (route[i] != NULL) {
            route6_conf_destroy(route[i]);
            route[i] = NULL;
        }
    }
}

static INLINE struct route6_item *_api_route6_item_alloc(int count)
{
    struct route6_item *item = NULL;

    item = dpdk_malloc(sizeof(*item) * count);
    if (item == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(item, 0, count * sizeof(*item));
    return item;
}

static INLINE bool _api_route6_is_valid_subnet(const struct dpdk_ip6_addr *addr, uint8_t mask)
{
    struct dpdk_ip6_addr mask_addr = *addr;
    dpdk_ip6_addr_subnet(&mask_addr, mask);
    return dpdk_ip6_addr_eq(addr, &mask_addr);
}

static int _api_route6_post_parse(struct route6_item **pp_item, int *p_count, void *json)
{
    int code = 0;
    size_t count = 0;
    void *array = NULL;
    char ip_str[CACHE_LINE] = "";
    struct route6_item *item = NULL;

    code = api_v1_modify_list(&array, &count, json, API_ROUTE6_MODULE_NAME, API_ROUTE6_LIST_NAME);
    if (code != 0) {
        return code;
    }

    item = _api_route6_item_alloc(count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        void *obj = NULL;
        uint8_t mask = 0;
        const char *net = NULL;
        const char *name = NULL;
        const char *nexthop = NULL;
        struct route6_item *one = NULL;

        obj = json_array_get(array, i);
        one = &item[i];

        net = json_string_value(json_object_get(obj, "net"));
        mask = json_integer_value(json_object_get(obj, "mask"));

        inet_pton(AF_INET6, net, &one->dst_subnet);
        one->mask = mask;

        nexthop = json_string_value(json_object_get(obj, "nexthop"));
        inet_pton(AF_INET6, nexthop, &one->nexthop);

        name = json_string_value(json_object_get(obj, "interface"));
        one->port = dpdk_port_by_name_get(name);
        if (one->port == UINT8_MAX) {
            code = ERRCODE_PORT_NOT_EXIST;
            goto _quit;
        }

        one->route_type = ROUTE6_MANUAL;

        if (!_api_route6_is_valid_subnet(&one->dst_subnet, one->mask)) {
            inet_ntop(AF_INET6, &one->dst_subnet, ip_str, sizeof(ip_str));
            LOG_ERROR("Invalid subnet(%s).", ip_str);
            code = ERRCODE_SUBNET_INVALID;
            goto _quit;
        }

        if (!dpdk_port_is_up(one->port)) {
            LOG_ERROR("Ethdev port(%d) startup failure.", one->port);
            code = ERRCODE_PORT_IS_DOWN;
            goto _quit;
        }
    }

    *pp_item = item;
    *p_count = count;

    return 0;

_quit:
    _api_route6_item_free(item);
    return code;
}

static int _api_route6_del_parse(struct route6_item **pp_item, int *p_count, void *json)
{
    int code = 0;
    size_t count = 0;
    void *array = NULL;
    struct route6_item *item = NULL;

    code = api_v1_delete_list(&array, &count, json, API_ROUTE6_MODULE_NAME, API_ROUTE6_LIST_NAME);
    if (code != 0) {
        return code;
    }

    item = _api_route6_item_alloc(count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        uint8_t mask = 0;
        json_t *obj = NULL;
        const char *net = NULL;
        struct route6_item *one = NULL;

        obj = json_array_get(array, i);
        one = &item[i];

        net = json_string_value(json_object_get(obj, "net"));
        mask = json_integer_value(json_object_get(obj, "mask"));

        inet_pton(AF_INET6, net, &one->dst_subnet);

        one->mask = mask;
        one->route_type = ROUTE6_MANUAL;
    }

    *pp_item = item;
    *p_count = count;

    return 0;
}

static int _api_route6_table_add(struct root *root, void *route6[], struct route6_item *item, int count)
{
    int code = 0;
    int hw_numa_id = 0;
    void *route6_table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        hw_numa_id = dp->hw_numa_id;
        route6_table = dp->tc->route6_table;

        code = route6_conf_create_and_append(&route6[i], route6_table, item, count, hw_numa_id, dp->tc->ip6_table);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_route6_table_free(route6, cpu_count);
    return code;
}

static int _api_route6_table_del(struct root *root, void *route6[], struct route6_item *item, int count)
{
    int code = 0;
    int hw_numa_id = 0;
    void *route6_table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        hw_numa_id = dp->hw_numa_id;
        route6_table = dp->tc->route6_table;

        code = route6_conf_create_and_delete(&route6[i], route6_table, item, count, hw_numa_id, true);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_route6_table_free(route6, cpu_count);
    return code;
}

static void _api_route6_update(void *route[], struct root *root)
{
    struct dataplane *dp = NULL;
    void **position[CPU_MAX] = {NULL};
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = (void **)&dp->tc->route6_table;
    }

    api_thread_config_update(root, position, route, route6_conf_destroy);
}

API_POST(/v1/network/route6, route6)
{
    int code = 0;
    int count = 0;
    struct root *root = cfg;
    void *route[CPU_MAX] = {NULL};
    struct route6_item *item = NULL;

    code = _api_route6_post_parse(&item, &count, json);
    if (code != 0) {
        return api_fail(code);
    }

    code = _api_route6_table_add(cfg, route, item, count);
    if (code != 0) {
        goto _quit;
    }

    _api_route6_update(route, root);
    _api_route6_item_free(item);

    LOG_DEBUG("CONFIG ROUTE6 SUCCESS.");
    return api_succ(NULL);

_quit:
    _api_route6_item_free(item);
    return api_fail(code);
}

API_PUT(/v1/network/route6, route6)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/route6, route6)
{
    int code = 0;
    int count = 0;
    struct root *root = cfg;
    void *route[NUMA_MAX] = {NULL};
    struct route6_item *item = NULL;

    code = _api_route6_del_parse(&item, &count, json);
    if (code != 0) {
        return api_fail(code);
    }

    code = _api_route6_table_del(cfg, route, item, count);
    if (code != 0) {
        goto _quit;
    }

    _api_route6_update(route, root);
    _api_route6_item_free(item);

    return api_succ(NULL);

_quit:
    _api_route6_item_free(item);
    return api_fail(code);
}

API_GET(/v1/network/route6, route6)
{
    int code = 0;
    int count = 0;
    void *array = NULL;
    struct root *root = cfg;
    void *route_table = NULL;
    struct dataplane *dp = NULL;
    struct route6_item *items = NULL;
    const char *route_type[] = {
        "DIRECT",
        "STATIC",
        "BGP",
        "OSPF",
        "ISIS",
        "RIP",
    };

    dp = root->dpdk_thread[0];
    route_table = dp->tc->route6_table;
    route6_conf_table_get(route_table, &items, &count);

    array = json_array();
    if (array == NULL) {
        return api_fail(ERRCODE_OOM);
    }

    for (int i = 0; i < count; i++) {
        void *one = NULL;
        const char *name = NULL;
        char ip_str[CACHE_LINE] = "";
        struct route6_item *item = NULL;

        item = &items[i];

        one = json_object();
        if (one == NULL) {
            LOG_ERROR("OOM");
            code = ERRCODE_OOM;
            goto _quit;
        }

        inet_ntop(AF_INET6, &item->dst_subnet, ip_str, sizeof(ip_str));
        api_json_add_string(one, "dst_subnet", ip_str);
        api_json_add_long(one, "mask", item->mask);

        inet_ntop(AF_INET6, &item->nexthop, ip_str, sizeof(ip_str));
        api_json_add_string(one, "nexthop", ip_str);

        name = dpdk_port_id_to_name(item->port);
        api_json_add_string(one, "interface", name);

        api_json_add_string(one, "owner", route_type[item->route_type]);

        code = json_array_append_new(array, one);
        if (code != 0) {
            code = ERRCODE_OOM;
            json_decref(one);
            goto _quit;
        }
    }

    return api_succ(array);

_quit:
    json_decref(array);
    return api_fail(code);
}