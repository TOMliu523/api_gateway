/*****************************************************************************
 * filename: api_ip6.c
 * function:
 * description:
 *****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/netfilter.h>

#include <jansson.h>
#include <sysrepo.h>

#include "l2.h"
#include "log.h"
#include "type.h"
#include "errcode.h"
#include "ip6_conf.h"
#include "protocol.h"
#include "dpdk_ip6.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_common.h"
#include "route6_conf.h"

#define API_INTERFACE_FORMAT "/v1:ip6/entrys[name='%s']/*"

struct api_ip6 {
    const char *name;
    uint16_t port;
    int mask;
    enum IP_TYPE type;
    struct dpdk_mac mac;
    struct dpdk_ip6_addr addr;
};

static int s_ndp_thread_id = -1;

static INLINE void _api_ip6_ndp_free(void **ndp, int count)
{
    dpdk_pktmbuf_push(ndp, count);
}

static void _api_ip6_free(void *ptr)
{
    if (ptr != NULL) {
        dpdk_free(ptr);
    }
}

static void *_api_ip6_alloc(size_t size)
{
    void *tmp = NULL;

    tmp = dpdk_malloc(size);
    if (tmp == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(tmp, 0, size);
    return tmp;
}

static void _api_ip6_manage_numa_free(void *manage[], int count)
{
    for (int i = 0; i < count; i++) {
        if (manage[i] != NULL) {
            ip6_conf_manage_destroy(manage[i]);
        }
    }
}

static void _api_ip6_route_numa_free(void *ptr[], int count)
{
    for (int i = 0; i < count; i++) {
        if (ptr[i] != NULL) {
            route6_conf_destroy(ptr[i]);
        }
    }
}

static int _api_ip6_obj_gen(void *array, sr_val_t *val, int cnt)
{
    int i = 0;
    int j = 0;
    int m = 0;
    int ret = 0;
    int group = 0;
    void *obj = NULL;
    const int ele_cnt = 4;

    group = cnt / ele_cnt;
    for (i = 0; i < group; i++) {
        obj = json_object();
        if (obj == NULL) {
            LOG_ERROR("OOM");
            return ERRCODE_OOM;
        }

        m = j + ele_cnt;
        for (; j < m; j++) {
            size_t len = 0;
            const char *xpath = val[j].xpath;

            len = strlen(xpath);
            if (len >= 3 && strcmp(xpath + len - 3, "/ip") == 0) {
                ret = api_json_add_string(obj, "ip", val[j].data.string_val);
            } else if (len >= 5 && strcmp(xpath + len - 5, "/mask") == 0) {
                ret = api_json_add_integer(obj, "mask", val[j].data.uint8_val);
            } else if (len >= 5 && strcmp(xpath + len - 5, "/type") == 0) {
                ret = api_json_add_string(obj, "type", val[j].data.string_val);
            } else if (len >= 5 && strcmp(xpath + len - 5, "/name") == 0) {
                ret = api_json_add_string(obj, "name", val[j].data.string_val);
            } else {
                LOG_ERROR("Not exists element(%s)", xpath);
                goto _quit;
            }

            if (ret != 0) {
                goto _quit;
            }
        }

        ret = json_array_append_new(array, obj);
        if (ret != 0) {
            LOG_ERROR("OOM");
            goto _quit;
        }
    }

    return 0;

_quit:
    if (obj != NULL) {
        json_decref(obj);
    }

    return ERRCODE_OOM;
}

static int _api_ip6_post_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_modify_list(json, "ip6", "entrys");
    return json_array_size(array);
}

static int _api_ip4_del_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_delete_list(json, "ip6", "entrys");
    return json_array_size(array);
}

static int _api_ip6_del_parse(struct root *root, void *json, struct api_ip6 *ip6, int count)
{
    int ret = 0;
    void *array = NULL;
    struct api_ip6 *one = NULL;

    array = api_v1_delete_list(json, "ip6", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;

        one = &ip6[i];
        json = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX) -1) {
            LOG_ERROR("Not exists(%s)", ip6->name);
            return ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        inet_pton(AF_INET6, ip, &one->addr);
    }

    return 0;
}

static int _api_ip6_post_parse(struct root *root, void *json, struct api_ip6 ip6[], int count)
{
    int ret = 0;
    void *array = NULL;
    struct api_ip6 *one = NULL;

    array = api_v1_modify_list(json, "ip6", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;
        const char *ip_type = NULL;

        one = &ip6[i];
        obj = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX)-1) {
            LOG_ERROR("Not exists(%s)", one->name);
            return ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        inet_pton(AF_INET6, ip, &one->addr);

        one->mask = json_integer_value(json_object_get(obj, "mask"));
        ret = l2_port_mac(one->port, &one->mac);
        if (ret != 0) {
            return ERRCODE_INNER;
        }

        ip_type = json_string_value(json_object_get(obj, "type"));
        if (strcmp(ip_type, "IP_MASTER") == 0) {
            one->type = IP_MASTER;
        } else {
            one->type = IP_SECONDARY;
        }

        if (!dpdk_port_is_up(one->port)) {
            LOG_ERROR("Port %d is down", one->port);
            return ERRCODE_PORT_IS_DOWN;
        }
    }

    return 0;
}

static int _api_ip6_ndp_gen(struct root *root, void *ndp[], struct api_ip6 ip6[], int count)
{
    int id = 0;
    int ret = 0;
    enum ERRCODE code = 0;
    struct api_ip6 *one = NULL;

    id = (s_ndp_thread_id + 1) % root->hw_info.cpu_count;
    s_ndp_thread_id = id;

    ret = dpdk_pktmbuf_pop(root->dpdk_thread[id]->pktmbuf_pool, ndp, count);
    if (ret != 0) {
        LOG_ERROR("Resource busy.");
        return ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < count; i++) {
        one = &ip6[i];

        ret = ip6_ndp_advertisement_gen(ndp[i], one->port, &one->addr, &one->mac);
        if (ret != 0) {
            _api_ip6_ndp_free(ndp, count);
            return ERRCODE_INNER;
        }

        DPDK_HEADROOM(ndp[i])->type = PKT_MBUF_NDP;
    }

    return 0;
}

static struct ip6_info *_api_ip6_to_info(struct api_ip6 *ip6, int count)
{
    struct ip6_info *info = NULL;

    info = _api_ip6_alloc(sizeof(*info) * count);
    if (info == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        info[i].addr = ip6[i].addr;
        info[i].mask = ip6[i].mask;
        info[i].port = ip6[i].port;
        info[i].type = ip6[i].type;
    }

    return info;
}

static INLINE void _api_ip6_to_subnet(struct dpdk_ip6_addr *addr, const struct dpdk_ip6_addr *ip6, uint8_t mask)
{
    *addr = *ip6;
    dpdk_ip6_addr_subnet(addr, mask);
}

static struct route6_item *_api_ip6_to_route_item(const struct api_ip6 *ip6, int count)
{
    struct route6_item *item = NULL;

    item = _api_ip6_alloc(sizeof(*item) * count);
    if (item == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        INIT_LIST_HEAD(&item[i].lru_head);
        dpdk_ip6_addr_unspec(&item[i].nexthop);
        _api_ip6_to_subnet(&item[i].dst_subnet, &ip6[i].addr, ip6[i].mask);
        item[i].mask = ip6[i].mask;
        item[i].route_type = ROUTE6_DIRECT;
        item[i].interface = ip6[i].port;
        item[i].valid = 1;
    }

    return item;
}

static INLINE int _api_ip6_manage_del(struct root *root, void *ip6_manage[], struct api_ip6 *ip6, int count)
{
    int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct ip6_info *info = NULL;

    info = _api_ip6_to_info(ip6, count);
    if (info == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    ret = ip6_conf_manage_create_and_delete(&ip6_manage[dp->numa_id], dp->tc->ip6_manage, info, count, dp->hw_numa_id);
    _api_ip6_free(info);
    if (ret != 0) {
        return ret;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        ret = ip6_conf_manage_create_and_append(&ip6_manage[i], ip6_manage[dp->numa_id], NULL, 0, rte_socket_id_by_idx(i));
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip6_manage_numa_free(ip6_manage, numa_count);
    return ret;
}

static int _api_ip6_manage_add(struct root *root, void *ip6_manage[], struct api_ip6 *ip6, int count)
{
    int ret = 0;
    struct dataplane *dp = NULL;
    struct dataplane *one = NULL;
    struct ip6_info *info = NULL;

    info = _api_ip6_to_info(ip6, count);
    if (info == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    ret = ip6_conf_manage_create_and_append(&ip6_manage[dp->numa_id], dp->tc->ip6_manage, info, count, dp->hw_numa_id);
    _api_ip6_free(info);
    if (ret != 0) {
        return ret;
    }

    for (int i = 0; i < root->hw_info.numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        one = root->dpdk_thread[i];
        ret = ip6_conf_manage_create_and_append(&ip6_manage[i], ip6_manage[dp->numa_id], NULL, 0, one->hw_numa_id);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip6_manage_numa_free(ip6_manage, count);
    return ret;
}

static int _api_ip6_route_table_del(struct root *root, void *route6[], const struct api_ip6 ip6[], int count, const void *arg)
{
   int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct route6_item *item = NULL;
    struct proto_header *proto = NULL;

    item = _api_ip6_to_route_item(ip6, count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    proto = dp->protocol;
    ret = route6_conf_create_and_delete(&route6[dp->numa_id], proto->route6, item, count, dp->hw_numa_id, false);
    _api_ip6_free(item);
    if (ret != 0) {
        goto _quit;
    }

    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        ret = route6_conf_create_and_append(&route6[i], &route6[dp->numa_id], NULL, 0, root->dpdk_thread[i]->hw_numa_id, arg);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip6_route_numa_free(route6, numa_count);
    return ret;
}

static int _api_ip6_route_table_add(struct root *root, void *route6[], const struct api_ip6 ip6[], int count, const void *arg)
{
    int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct dataplane *one = NULL;
    struct route6_item *item = NULL;
    struct proto_header *proto = NULL;

    item = _api_ip6_to_route_item(ip6, count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    proto = dp->protocol;
    ret = route6_conf_create_and_append(&route6[dp->numa_id], proto->route6, item, count, dp->hw_numa_id, arg);
    _api_ip6_free(item);
    if (ret != 0) {
        return ret;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        one = root->dpdk_thread[i];
        ret = route6_conf_create_and_append(&route6[i], route6[dp->numa_id], NULL, 0, one->hw_numa_id, arg);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip6_route_numa_free(route6, numa_count);
    return ret;
}

static INLINE void _api_ip6_ndp_send(struct root *root, void *ndp[], int count)
{
    struct dataplane *dp = root->dpdk_thread[s_ndp_thread_id];
    dpdk_ring_mp_push(dp->notice_ring, ndp, count);
}

API_POST(/v1/network/ip6, ip6)
{
    int count = 0;
    void **ndp = NULL;
    enum ERRCODE code = 0;
    struct root *root = cfg;
    struct api_ip6 *ip6 = NULL;
    void *route6[NUMA_MAX] = {NULL};
    void **position[CPU_MAX] = {NULL};
    void *ip6_manage[NUMA_MAX] = {NULL};
    void *thread_route6[CPU_MAX] = {NULL};
    void *thread_ip6_manage[CPU_MAX] = {NULL};

    count = _api_ip6_post_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(ERRCODE_INVALID);
    }

    ip6 = _api_ip6_alloc(count * sizeof(*ip6));
    if (ip6 == NULL) {
        goto _quit;
    }

    ndp = _api_ip6_alloc(count * sizeof(*ndp));
    if (ndp == NULL) {
        goto _quit;
    }

    code = _api_ip6_post_parse(cfg, json, ip6, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_ndp_gen(cfg, ndp, ip6, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_manage_add(cfg, ip6_manage, ip6, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_route_table_add(cfg, route6, ip6, count, ip6_manage[0]);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        position[i] = &root->dpdk_thread[i]->tc->ip6_manage;
        thread_ip6_manage[i] = ip6_manage[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_ip6_manage, _api_ip6_manage_numa_free);

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        struct proto_header *proto = root->dpdk_thread[i]->protocol;
        position[i] = (void **) proto->route6;
        thread_route6[i] = route6[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_route6, _api_ip6_route_numa_free);

    _api_ip6_ndp_send(cfg, ndp, count);
    _api_ip6_free(ip6);
    _api_ip6_free(ndp);

    LOG_DEBUG("CONFIG IP6 SUCCESS.");
    return api_succ(NULL);

_quit:
    _api_ip6_free(ip6);
    if (ndp != NULL && ndp[0] != NULL) {
        _api_ip6_ndp_free(ndp, count);
    }
    _api_ip6_free(ndp);
    return api_fail(code);
}

API_PUT(/v1/network/ip6, ip6)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/ip6, ip6)
{
    int count = 0;
    enum ERRCODE code = 0;
    struct root *root = cfg;
    struct api_ip6 *api_iface = {0};
    void *route6[NUMA_MAX] = {NULL};
    void **position[CPU_MAX] = {NULL};
    void *ip6_manage[NUMA_MAX] = {NULL};
    void *thread_route6[CPU_MAX] = {NULL};
    void *thread_ip6_manage[CPU_MAX] = {NULL};

    count = _api_ip4_del_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(ERRCODE_INVALID);
    }

    api_iface = _api_ip6_alloc(count);
    if (api_iface == NULL) {
        return api_fail(ERRCODE_OOM);
    }

    code = _api_ip6_del_parse(cfg, json, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_manage_del(cfg, ip6_manage, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_route_table_del(cfg, route6, api_iface, count, ip6_manage[0]);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        position[i] = &root->dpdk_thread[i]->tc->ip6_manage;
        thread_ip6_manage[i] = ip6_manage[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_ip6_manage, _api_ip6_manage_numa_free);

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        struct proto_header *proto = root->dpdk_thread[i]->protocol;
        position[i] = (void **)&proto->route6;
        thread_route6[i] = route6[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_route6, _api_ip6_route_numa_free);

    _api_ip6_free(api_iface);
    return api_succ(NULL);

_quit:
    _api_ip6_free(api_iface);
    return api_fail(code);
}

API_GET(/v1/network/ip6, ip6)
{
    int ret = 0;
    void *array = NULL;
    size_t val_cnt = 0;
    sr_val_t *val = NULL;
    char path[2 * CACHE_LINE + 1] = {0};
    const struct port_name *port_name = NULL;
    const struct port_name_entry *one = NULL;

    array = json_array();
    if (array == NULL) {
        LOG_ERROR("OOM.");
        return api_fail(ERRCODE_INNER);
    }

    port_name = dpdk_port_name_get();
    for (int i = 0; i < port_name->count; i++) {
        one = &port_name->entrys[i];

        snprintf(path, sizeof(path), API_INTERFACE_FORMAT, one->name);
        ret = sr_get_items(sess, path, 0, 0, &val, &val_cnt);
        if (ret != 0 && ret != SR_ERR_NOT_FOUND) {
            LOG_ERROR("Failure path(%s) sr_get_item: %s", path, strerror(-ret));
            goto _quit;
        }

        ret = _api_ip6_obj_gen(array, val, val_cnt);
        if (ret != 0) {
            goto _quit;
        }

        sr_free_values(val, val_cnt);
    }

    return api_succ(NULL);

_quit:
    if (array != NULL) {
        json_decref(array);
    }
    if (val != NULL) {
        sr_free_values(val, val_cnt);
    }
    return api_fail(ERRCODE_INNER);
}