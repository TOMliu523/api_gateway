/*****************************************************************************
 * filename: api_ip.c
 * function:
 * description:
 ****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/netfilter.h>

#include <jansson.h>
#include <sysrepo.h>

#include "log.h"
#include "ip4.h"
#include "type.h"
#include "errcode.h"
#include "dpdk_ip.h"
#include "protocol.h"
#include "ip4_conf.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_type.h"
#include "dpdk_core.h"
#include "dpdk_common.h"
#include "route4_conf.h"

#define API_INTERFACE_FORMAT "/v1:ip4/entrys[name='%s']/*"

struct api_ip4 {
    const char *name;
    uint16_t port;
    int mask;
    enum IP_TYPE ip_type;
    uint32_t ip;
    struct dpdk_mac mac;
};

static int s_arp_thread_id = -1;

static int _api_ip4_obj_gen(void *array, sr_val_t *val, int cnt)
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

static INLINE void _api_ip4_broadcast_free(void **arp, int count)
{
    dpdk_pktmbuf_push(arp, count);
}

static INLINE void _api_ip4_manage_numa_free(void *manage[], int count)
{
    for (int i = 0; i < count; i++) {
        if (manage[i] != NULL) {
            ip4_manage_destroy(manage[i]);
        }
    }
}

static INLINE void _api_ip4_route_numa_free(void *route[], int count)
{
    for (int i = 0; i < count; i++) {
        if (route[i] != NULL) {
            route4_conf_destroy(route[i]);
        }
    }
}

static INLINE void _api_ip4_info_free(struct ip4_info *info)
{
    if (info == NULL) {
        return;
    }

    dpdk_free(info);
}

static INLINE void _api_ip4_free(void *ptr)
{
    dpdk_free(ptr);
}

static INLINE void *_api_ip4_alloc(size_t total)
{
    void *tmp = NULL;

    tmp = dpdk_malloc(total);
    if (tmp == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(tmp, 0, total);
    return tmp;
}

static enum ERRCODE _api_ip4_broadcast_gen(struct root *root, void **arp, struct api_ip4 *iface, int count)
{
    int id = 0;
    int ret = 0;
    enum ERRCODE code = 0;
    struct api_ip4 *one = NULL;

    id = (s_arp_thread_id + 1) % root->hw_info.cpu_count;
    s_arp_thread_id = id;

    ret = dpdk_pktmbuf_pop(root->dpdk_thread[id]->pktmbuf_pool, arp, count);
    if (ret != 0) {
        LOG_ERROR("Resource busy");
        return ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < count; i++) {
        one = &iface[i];

        ret = l2_gratuitous_arp_gen(arp[i], one->port, one->ip, &one->mac);
        if (ret != 0) {
            _api_ip4_broadcast_free(arp, count);
            return ERRCODE_INNER;
        }

        DPDK_HEADROOM(arp[i])->type = PKT_MBUF_GARP;
    }

    return ERRCODE_SUCCESS;
}

static INLINE int _api_ip4_post_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_modify_list(json, "ip4", "entrys");
    return json_array_size(array);
}

static INLINE int _api_ip4_del_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_delete_list(json, "ip4", "entrys");
    return json_array_size(array);
}

static enum ERRCODE _api_ip4_post_parse(struct root *root, void *json, struct api_ip4 *iface, int count)
{
    int af = 0;
    int ret = 0;
    void *array = NULL;
    struct api_ip4 *one = NULL;

    array = api_v1_modify_list(json, "ip", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;
        const char *ip_type = NULL;

        one = &iface[i];
        obj = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX)-1) {
            LOG_ERROR("Not exists(%s)", one->name);
            return ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        inet_pton(AF_INET, ip, &one->ip);

        one->mask = json_integer_value(json_object_get(obj, "mask"));
        ret = l2_port_mac(one->port, &one->mac);
        if (ret != 0) {
            return ERRCODE_INNER;
        }

        ip_type = json_string_value(json_object_get(obj, "type"));
        if (strcmp(ip_type, "IP_MASTER") == 0) {
            one->ip_type = IP_MASTER;
        } else {
            one->ip_type = IP_SECONDARY;
        }

        if (!dpdk_port_is_up(one->port)) {
            LOG_ERROR("Port %d is down", one->port);
            return ERRCODE_PORT_IS_DOWN;
        }
    }

    return 0;
}

static int _api_ip4_del_parse(struct root *root, void *json, struct api_ip4 *iface, int count)
{
    int ret = 0;
    void *array = NULL;
    struct api_ip4 *one = NULL;

    array = api_v1_delete_list(json, "ip4", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;

        one = &iface[i];
        obj = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX) -1) {
            LOG_ERROR("Not exists(%s)", iface->name);
            return ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        inet_pton(AF_INET, ip, &one->ip);
    }

    return 0;
}

static INLINE void *_api_ip4_to_info(const struct api_ip4 *iface, int count)
{
    struct ip4_info *info = NULL;

    info = dpdk_malloc(count * sizeof(*info));
    if (info == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(info, 0, count * sizeof(*info));

    for (int i = 0; i < count; i++) {
        info[i].ip = iface[i].ip;
        info[i].mask = iface[i].mask;
        INIT_LIST_HEAD(&info[i].node);
        info[i].port = iface[i].port;
        info[i].type = iface[i].ip_type;
    }

    return info;
}

static INLINE uint32_t _api_ip4_to_subnet(uint32_t ip_be, int mask)
{
    uint32_t ip = dpdk_be_to_cpu_32(ip_be);
    uint32_t ip_mask = L3_MASK_TO_IP(mask);

    return dpdk_cpu_to_be_32(ip & ip_mask);
}

static INLINE void *_api_ip4_to_route_item(const struct api_ip4 *iface, int count)
{
    struct route4_item *item = NULL;

    item = dpdk_malloc(count * sizeof(*item));
    if (item == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(item, 0, sizeof(*item));

    for (int i = 0; i < count; i++) {
        INIT_LIST_HEAD(&item[i].lru_head);
        item[i].nexthop = 0;
        item[i].dst_subnet = _api_ip4_to_subnet(iface[i].ip, iface[i].mask);
        item[i].mask = iface[i].mask;
        item[i].route_type = ROUTE4_DIRECT;
        item[i].priority = 0;
        item[i].interface = iface[i].port;
        item[i].last_access_time = 0;
        item[i].last_probe_time = 0;
        item[i].valid = 1;
        item[i].direct_id = 0;
    }

    return item;
}

static INLINE int _api_ip4_manage_del(struct root *root, void *ip4_manage[], struct api_ip4 *iface, int count)
{
    int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct ip4_info *info = NULL;

    info = _api_ip4_to_info(iface, count);
    if (info == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    ret = ip4_conf_manage_create_and_delete(&ip4_manage[dp->numa_id], dp->tc->ip4_manage, info, count, dp->hw_numa_id);
    _api_ip4_info_free(info);
    if (ret != 0) {
        return ret;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        ret = ip4_conf_manage_create_and_append(&ip4_manage[i], ip4_manage[dp->numa_id], NULL, 0, rte_socket_id_by_idx(i));
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip4_manage_numa_free(ip4_manage, numa_count);
    return ret;
}

static INLINE int _api_ip4_manage_add(struct root *root, void *ip4_manage[], const struct api_ip4 *iface, int count)
{
    int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct dataplane *one = NULL;
    struct ip4_info *info = NULL;

    info = _api_ip4_to_info(iface, count);
    if (info == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    ret = ip4_conf_manage_create_and_append(&ip4_manage[dp->numa_id], dp->tc->ip4_manage, info, count, dp->hw_numa_id);
    _api_ip4_info_free(info);
    if (ret != 0) {
        return ret;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        one = root->dpdk_thread[i];
        if (i == dp->numa_id) {
            continue;
        }

        ret = ip4_conf_manage_create_and_append(&ip4_manage[i], ip4_manage[dp->numa_id], NULL, 0, one->hw_numa_id);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip4_manage_numa_free(ip4_manage, numa_count);
    return ret;
}

static int _api_ip4_route_table_add(struct root *root, void *route[], const struct api_ip4 *iface, int count, const void *arg)
{
    int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct route4_item *item = NULL;
    struct proto_header *proto = NULL;

    item = _api_ip4_to_route_item(iface, count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    proto = dp->protocol;
    ret = route4_conf_create_and_append(&route[dp->numa_id], proto->route4, item, count, dp->hw_numa_id, arg);
    _api_ip4_free(item);
    if (ret != 0) {
        return ret;
    }

    numa_count = root->hw_info.numa_count;
    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        ret = route4_conf_create_and_append(&route[i], route[dp->numa_id], NULL, 0, root->dpdk_thread[i]->hw_numa_id, arg);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip4_route_numa_free(route, numa_count);
    return ret;
}

static int _api_ip4_route_table_del(struct root *root, void *route[], const struct api_ip4 *iface, int count, const void *arg)
{
    int ret = 0;
    int numa_count = 0;
    struct dataplane *dp = NULL;
    struct route4_item *item = NULL;
    struct proto_header *proto = NULL;

    item = _api_ip4_to_route_item(iface, count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    dp = root->dpdk_thread[0];
    proto = dp->protocol;
    ret = route4_conf_create_and_delete(&route[dp->numa_id], proto->route4, item, count, dp->hw_numa_id, false);
    _api_ip4_free(item);
    if (ret != 0) {
        goto _quit;
    }

    for (int i = 0; i < numa_count; i++) {
        if (i == dp->numa_id) {
            continue;
        }

        ret = route4_conf_create_and_append(&route[i], &route[dp->numa_id], NULL, 0, root->dpdk_thread[i]->hw_numa_id, arg);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_ip4_route_numa_free(route, numa_count);
    return ret;
}

static INLINE void _api_ip4_arp_send(struct root *root, void *arp_mbuf[], int count)
{
    struct dataplane *dp = root->dpdk_thread[s_arp_thread_id];
    dpdk_ring_mp_push(dp->notice_ring, arp_mbuf, count);
}

API_POST(/v1/network/ip4, ip4)
{
    int count = 0;
    void **arp = NULL;
    enum ERRCODE code = 0;
    struct root *root = cfg;
    struct api_ip4 *api_iface = {0};
    void *route[NUMA_MAX] = {NULL};
    void **position[CPU_MAX] = {NULL};
    void *ip4_manage[NUMA_MAX] = {NULL};
    void *thread_route[CPU_MAX] = {NULL};
    void *thread_ip4_manage[CPU_MAX] = {NULL};

    count = _api_ip4_post_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(ERRCODE_INVALID);
    }

    arp = _api_ip4_alloc(count * sizeof(*arp));
    if (arp == NULL) {
        goto _quit;
    }

    api_iface = _api_ip4_alloc(count * sizeof(*api_iface));
    if (api_iface == NULL) {
        goto _quit;
    }

    code = _api_ip4_post_parse(cfg, json, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_broadcast_gen(cfg, arp, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_manage_add(cfg, ip4_manage, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_route_table_add(cfg, route, api_iface, count, ip4_manage[0]);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        position[i] = &root->dpdk_thread[i]->tc->ip4_manage;
        thread_ip4_manage[i] = ip4_manage[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_ip4_manage, _api_ip4_manage_numa_free);

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        struct proto_header *proto = root->dpdk_thread[i]->protocol;
        position[i] = (void **)&proto->route4;
        thread_route[i] = route[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_route, _api_ip4_route_numa_free);

    _api_ip4_arp_send(cfg, arp, count);
    _api_ip4_free(api_iface);
    _api_ip4_free(arp);

    LOG_DEBUG("CONFIG IP4 SUCCESS.");
    return api_succ(NULL);

_quit:
    if (arp != NULL && arp[0] != NULL) {
        _api_ip4_broadcast_free(arp, count);
    }
    _api_ip4_free(arp);
    _api_ip4_free(api_iface);
    // _api_ip_route_numa_free(route, root->hw_info.numa_count);
    _api_ip4_manage_numa_free(ip4_manage, root->hw_info.numa_count);
    return api_fail(code);
}

API_PUT(/v1/network/ip4, ip4)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/ip4, ip4)
{
    int count = 0;
    enum ERRCODE code = 0;
    struct root *root = cfg;
    struct api_ip4 *api_iface = {0};
    void *route[NUMA_MAX] = {NULL};
    void **position[CPU_MAX] = {NULL};
    void *ip4_manage[NUMA_MAX] = {NULL};
    void *thread_route[CPU_MAX] = {NULL};
    void *thread_ip4_manage[CPU_MAX] = {NULL};

    count = _api_ip4_del_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(ERRCODE_INVALID);
    }

    api_iface = _api_ip4_alloc(count);
    if (api_iface == NULL) {
        return api_fail(ERRCODE_OOM);
    }

    code = _api_ip4_del_parse(cfg, json, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_manage_del(cfg, ip4_manage, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_route_table_del(cfg, route, api_iface, count, ip4_manage[0]);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        position[i] = &root->dpdk_thread[i]->tc->ip4_manage;
        thread_ip4_manage[i] = ip4_manage[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_ip4_manage, _api_ip4_manage_numa_free);

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        struct proto_header *proto = root->dpdk_thread[i]->protocol;
        position[i] = (void **)&proto->route4;
        thread_route[i] = route[root->dpdk_thread[i]->numa_id];
    }

    api_config_update(cfg, position, thread_route, _api_ip4_route_numa_free);

    _api_ip4_free(api_iface);
    return api_succ(NULL);

_quit:
    _api_ip4_free(api_iface);
    return api_fail(code);
}

API_GET(/v1/network/ip4, ip4)
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

        ret = _api_ip4_obj_gen(array, val, val_cnt);
        if (ret != 0) {
            goto _quit;
        }

        sr_free_values(val, val_cnt);
    }

    return api_succ(array);

_quit:
    if (array != NULL) {
        json_decref(array);
    }

    if (val != NULL) {
        sr_free_values(val, val_cnt);
    }

    return api_fail(ERRCODE_INNER);
}