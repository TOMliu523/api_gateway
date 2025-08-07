/*****************************************************************************
 * filename: api_route.c
 * function:
 * description:
 *****************************************************************************/

#include <arpa/inet.h>

#include "log.h"
#include "type.h"
#include "route.h"
#include "errcode.h"
#include "protocol.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_common.h"

static INLINE void *_api_route_item_free(struct route_item *item)
{
    if (item != NULL) {
        dpdk_free(item);
    }
}

static INLINE void _api_route_numa_free(void *route[], int count)
{
    for (int i = 0; i < count; i++) {
        if (route[i] == NULL) {
            route_conf_destroy(route[i]);
        }
    }
}

static INLINE struct route_item *_api_route_item_alloc(int count)
{
    struct route_item *item = NULL;

    item = dpdk_malloc(sizeof(*item) * count);
    if (item == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(item, 0, count * sizeof(*item));
    return item;
}

static INLINE bool _api_route_is_valid_subnet(uint32_t ip_be, uint8_t mask)
{
    uint32_t ip = dpdk_be_to_cpu_32(ip_be);
    uint32_t ip_mask = L3_MASK_TO_IP(mask);

    return (ip & ~ip_mask) == 0 ? true : false;
}

static int _api_route_post_parse(struct route_item **pp_item, int *p_count, void *json)
{
    int af= 0;
    int code = 0;
    int count = 0;
    void *array = NULL;
    char ip_str[CACHE_LINE] = "";
    struct route_item *item = NULL;

    array = api_v1_modify_list(json, "route_v4", "entrys");
    count = json_array_size(array);

    item = _api_route_item_alloc(count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        void *obj = NULL;
        uint8_t mask = 0;
        const char *net = NULL;
        const char *name = NULL;
        const char *nexthop = NULL;
        struct route_item *one = NULL;

        obj = json_array_get(array, i);
        one = &item[i];

        net = json_string_value(json_object_get(obj, "net"));
        mask = json_integer_value(json_object_get(obj, "mask"));

        inet_pton(AF_INET, net, &one->dst_subnet);
        one->mask = mask;

        if (json_object_get(obj, "nexthop")) {
            nexthop = json_string_value(json_object_get(obj, "nexthop"));
            inet_pton(AF_INET, nexthop, &one->nexthop);
            one->interface = UINT8_MAX;
        } else {
            name = json_string_value(json_object_get(obj, "interface_name"));
            one->interface = dpdk_port_by_name_get(name);
        }

        one->route_type = ROUTE_MANUAL;

        if (!_api_route_is_valid_subnet(one->dst_subnet, one->mask)) {
            inet_ntop(AF_INET, &one->dst_subnet, ip_str, sizeof(ip_str));
            LOG_ERROR("Invalid subnet(%s).", ip_str);
            code = ERRCODE_SUBNET_INVALID;
            goto _quit;
        }

        if (!dpdk_port_is_up(one->interface)) {
            LOG_ERROR("Ethdev port(%d) startup failure.", one->interface);
            code = ERRCODE_PORT_IS_DOWN;
            goto _quit;
        }
    }

    *pp_item = item;
    *p_count = count;

    return 0;

_quit:
    _api_route_item_free(item);
    return code;
}

static int _api_route_del_parse(struct route_item **pp_item, int *p_count, void *json)
{
    int af = 0;
    int ret = 0;
    int count = 0;
    void *array = NULL;
    struct route_item *item = NULL;

    array = api_v1_delete_list(json, "route_v4", "entrys");
    count = json_array_size(array);

    item = _api_route_item_alloc(count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        uint8_t mask = 0;
        json_t *obj = NULL;
        const char *net = NULL;
        struct route_item *one = NULL;

        obj = json_array_get(array, i);
        one = &item[i];

        net = json_string_value(json_object_get(obj, "net"));
        mask = json_integer_value(json_object_get(obj, "mask"));

        inet_pton(AF_INET, net, &one->dst_subnet);

        item->mask = mask;
        item->route_type = ROUTE_MANUAL;
    }

    *pp_item = item;
    *p_count = count;
    return 0;

_quit:
    _api_route_item_free(item);
    return ret;
}

static int _api_route_table_add(struct root *root, void *route[], struct route_item *item, int count)
{
    int ret = 0;
    int numa_id = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct proto_header *proto = NULL;

    dp = root->dpdk_thread[0];
    proto = dp->protocol;
    ret = route_conf_create_and_append(&route[dp->numa_id], proto->route, item, count, dp->hw_numa_id, dp->tc->ipv4_manage);
    if (ret != 0) {
        return ret;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        numa_id = root->dpdk_thread[i]->numa_id;
        ret = route_conf_create_and_append(&route[i], route[dp->numa_id], NULL, 0, numa_id, dp->tc->ipv4_manage);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_route_numa_free(route, numa_count);
    return 0;
}

static int _api_route_table_del(struct root *root, void *route[], struct route_item *item, int count)
{
    int code = 0;
    int numa_id = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct proto_header *proto = NULL;

    dp = root->dpdk_thread[0];
    proto = dp->protocol;
    code = route_conf_create_and_delete(&route[dp->numa_id], proto->route, item, count, dp->hw_numa_id, true);
    if (code != 0) {
        return code;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        numa_id = root->dpdk_thread[i]->numa_id;
        code = route_conf_create_and_append(&route[i], route[dp->numa_id], NULL, 0, numa_id, dp->tc->ipv4_manage);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_route_numa_free(route, numa_count);
    return 0;
}

API_POST(/v1/network/route, route)
{
    int code = 0;
    int count = 0;
    struct root *root = cfg;
    void *route[NUMA_MAX] = {NULL};
    struct route_item *item = NULL;
    void **position[CPU_MAX] = {NULL};
    void *thread_route[CPU_MAX] = {NULL};

    code = _api_route_post_parse(&item, &count, json);
    if (code != 0) {
        return api_fail(code);
    }

    code = _api_route_table_add(cfg, route, item, count);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        struct proto_header *proto = root->dpdk_thread[i]->protocol;
        position[i] = (void **)&proto->route;
        thread_route[i] = route[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_route, _api_route_numa_free);
    _api_route_item_free(item);

    return api_succ(NULL);

_quit:
    _api_route_item_free(item);
    return api_fail(code);
}

API_PUT(/v1/network/route, route)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DELETE(/v1/network/route, route)
{
    int code = 0;
    int count = 0;
    struct root *root = cfg;
    void *route[NUMA_MAX] = {NULL};
    struct route_item *item = NULL;
    void **position[CPU_MAX] = {NULL};
    void *thread_route[CPU_MAX] = {NULL};

    code = _api_route_del_parse(&item, &count, json);
    if (code != 0) {
        return api_fail(code);
    }

    code = _api_route_table_del(cfg, route, item, count);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        struct proto_header *proto = root->dpdk_thread[i]->protocol;
        position[i] = (void **)&proto->route;
        thread_route[i] = route[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_route, _api_route_numa_free);

    _api_route_item_free(item);
    return api_succ(NULL);

_quit:
    _api_route_item_free(item);
    return api_fail(code);
}

API_GET(/v1/network/route, route)
{
    int code = 0;
    int count = 0;
    void *route = NULL;
    void *array = NULL;
    struct root *root = cfg;
    struct route_item *items = NULL;
    struct proto_header *proto = NULL;
    const char *route_type[] = {
        "DIRECT",
        "STATIC",
        "BGP",
        "OSPF",
        "ISIS",
        "RIP",
    };

    proto = root->dpdk_thread[0]->protocol;
    route = proto->route;
    route_conf_table_get(route, &items, &count);

    if (count == 0) {
        return api_succ(NULL);
    }

    array = json_array();
    for (int i = 0; i < count; i++) {
        void *one = NULL;
        const char *name = NULL;
        char ip_str[CACHE_LINE] = "";
        struct route_item *item = NULL;

        item = &items[i];

        one = json_object();
        if (one == NULL) {
            LOG_ERROR("OOM");
            code = ERRCODE_OOM;
            goto _quit;
        }

        inet_ntop(AF_INET, &item->dst_subnet, ip_str, sizeof(ip_str));
        api_json_add_string(one, "dst_subnet", ip_str);
        api_json_add_integer(one, "mask", item->mask);

        inet_ntop(AF_INET, &item->nexthop, ip_str, sizeof(ip_str));
        api_json_add_string(one, "nexthop", ip_str);

        name = dpdk_port_id_to_name(item->interface);
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