/*****************************************************************************
 * filename: api_interface.c
 * function:
 * description:
 ****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/netfilter.h>

#include <jansson.h>
#include <sysrepo.h>

#include "l2.h"
#include "l3.h"
#include "log.h"
#include "rcu.h"
#include "type.h"
#include "protocol.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_type.h"
#include "dpdk_core.h"
#include "dpdk_common.h"

#define API_INTERFACE_FORMAT "/v1:interface/entrys[name='%s']/*"

struct api_interface {
    const char *name;
    int af;
    uint16_t port;
    int mask;
    enum IP_TYPE ip_type;
    union dpdk_ip ip;
    struct dpdk_mac mac;
};

static int s_arp_thread_id = -1;

static void *_api_iface_obj_gen(const char *name, sr_val_t *val, int cnt)
{
    int ret = 0;
    void *obj = NULL;

    obj = json_object();
    if (obj == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    ret = api_json_add_string(obj, "name", name);
    if (ret != 0) {
        goto _quit;
    }

    for (int i = 0; i < cnt; i++) {
        size_t len = 0;
        const char *xpath = val[i].xpath;

        len = strlen(xpath);
        if (len >= 3 && strcmp(xpath + len - 3, "/ip") == 0) {
            ret = api_json_add_string(obj, "ip", val[i].data.string_val);
        } else if (len >= 5 && strcmp(xpath + len - 5, "/mask") == 0) {
            ret = api_json_add_integer(obj, "mask", val[i].data.uint8_val);
        } else if (len >= 5 && strcmp(xpath + len - 5, "/type") == 0) {
            ret = api_json_add_string(obj, "type", val[i].data.string_val);
        }

        if (ret != 0) {
            goto _quit;
        }
    }

    return obj;

_quit:
    if (obj != NULL) {
        json_decref(obj);
    }

    return NULL;
}

static INLINE void _api_iface_broadcast_free(void **arp, int count)
{
    dpdk_pktmbuf_push(arp, count);
}

static enum API_ERRCODE _api_iface_broadcast_gen(struct root *root, void **arp, struct api_interface *iface, int count)
{
    int id = 0;
    int ret = 0;
    enum API_ERRCODE code = 0;
    struct api_interface *one = NULL;

    id = (s_arp_thread_id + 1) % root->hw_info.cpu_count;
    s_arp_thread_id = id;

    ret = dpdk_pktmbuf_pop(root->dpdk_thread[id]->pktmbuf_pool, arp, count);
    if (ret != 0) {
        LOG_ERROR("Resource busy");
        return API_ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < count; i++) {
        one = &iface[i];

        ret = l2_gratuitous_arp_gen(arp[i], one->port, one->ip.ipv4, &one->mac);
        if (ret != 0) {
            _api_iface_broadcast_free(arp, count);
            return API_ERRCODE_INNER;
        }

        ((struct dpdk_data *)arp[i])->headroom.type = PKT_MBUF_GARP;
    }

    return API_ERRCODE_SUCCESS;
}

static INLINE int _api_iface_post_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_modify_list(json, "interface", "entrys");
    return json_array_size(array);
}

static INLINE int _api_iface_del_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_delete_list(json, "interface", "entrys");
    return json_array_size(array);
}

static INLINE void *_api_iface_alloc(int count)
{
    struct api_interface *iface = NULL;

    iface = malloc(count * sizeof(*iface));
    if (iface == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(iface, 0, count * sizeof(*iface));
    return iface;
}

static INLINE void _api_iface_free(void *ptr)
{
    if (ptr != NULL) {
        free(ptr);
    }
}

static enum API_ERRCODE _api_iface_post_parse(struct root *root, void *json, struct api_interface *iface, int count)
{
    int af = 0;
    int ret = 0;
    void *array = NULL;
    struct api_interface *one = NULL;

    array = api_v1_modify_list(json, "interface", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;
        const char *ip_type = NULL;

        one = &iface[i];
        obj = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX)-1) {
            LOG_ERROR("Not exists(%s)", iface->name);
            return API_ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        if (strchr(ip, ':') == NULL) {
            one->af = AF_INET;
        } else {
            one->af = AF_INET6;
        }

        if (af == 0) {
            af = one->af;
        } else if (af != one->af) {
            LOG_ERROR("Do not allow mixing of IPv4 and IPv6 configurations.");
            return API_ERRCODE_MIX_IP;
        }

        inet_pton(one->af, ip, &one->ip);

        one->mask = json_integer_value(json_object_get(obj, "mask"));
        ret = l2_port_mac(one->port, &one->mac);
        if (ret != 0) {
            return API_ERRCODE_INNER;
        }

        ip_type = json_string_value(json_object_get(obj, "type"));
        if (strcmp(ip_type, "IP_MASTER") == 0) {
            one->ip_type = IP_MASTER;
        } else {
            one->ip_type = IP_SECONDARY;
        }

        ret = dpdk_port_restart(one->port);
        if (ret != 0) {
            LOG_ERROR("Failure(dpdk_port_startup) port(%d), message: %s", one->port, strerror(-ret));
            return API_ERRCODE_INNER;
        }
    }

    return 0;
}

static enum API_ERRCODE _api_iface_del_parse(struct root *root, void *json, struct api_interface *iface, int count)
{
    int ret = 0;
    void *array = NULL;
    struct api_interface *one = NULL;

    array = api_v1_delete_list(json, "interface", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;

        one = &iface[i];
        obj = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX) -1) {
            LOG_ERROR("Not exists(%s)", iface->name);
            return API_ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        if (strchr(ip, ':') == NULL) {
            one->af = AF_INET;
        } else {
            one->af = AF_INET6;
        }

        inet_pton(one->af, ip, &one->ip);
    }

    return API_ERRCODE_SUCCESS;
}

static int _api_iface_ipv4_manage_copy(struct ipv4_manage *dst, struct ipv4_manage *src, int hw_numa_id)
{
    struct ipv4_info *one = NULL;
    struct ipv4_info *cur = NULL;
    struct ipv4_info *next = NULL;
    struct list_head *head = NULL;
    struct list_head *prev = NULL;

    if (src == NULL) {
        return 0;
    }

    for (int i = 0; i < ARR_NUMS(dst->head); i++) {
        head = &dst->head[i];

        prev = head;
        list_for_each_entry_safe(cur, next, &src->head[i], node) {
            one = dpdk_malloc_numa(sizeof(*one), hw_numa_id);
            if (UNLIKELY(one == NULL)) {
                LOG_ERROR("OOM.");
                return -1;
            }

            dpdk_memcpy(one, cur, sizeof(*one));
            list_add(&one->node, prev);

            prev = &one->node;

            if (cur == src->info[cur->port]) {
                dst->info[one->port] = one;
            }
        }
    }

    dst->ip_count = src->ip_count;
    return 0;
}

static void _api_iface_ipv4_manage_destruct(struct ipv4_manage **ipv4_manage, int count)
{
    for (int i = 0; i < count; i++) {
        l3_thread_ipv4_destroy(ipv4_manage[i]);
    }
}

static enum API_ERRCODE _api_iface_ipv4_manage_construct(struct root *root, struct ipv4_manage **ipv4_manage)
{
    int ret = 0;
    int nic_count = 0;
    int hw_numa_id = 0;
    struct ipv4_manage *one = NULL;
    struct ipv4_manage *ipv4 = NULL;
    int cpu_count = root->hw_info.cpu_count;

    nic_count = root->hw_info.nic_count;

    for (int i = 0; i < cpu_count; i++) {
        hw_numa_id = root->dpdk_thread[i]->hw_numa_id;

        one = ipv4_manage[i] = l3_thread_ipv4_create(hw_numa_id);
        if (one == NULL) {
            goto _quit;
        }

        ipv4 = root->dpdk_thread[i]->tc->ipv4_manage;
        ret = _api_iface_ipv4_manage_copy(one, ipv4, hw_numa_id);
        if (ret != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_iface_ipv4_manage_destruct(ipv4_manage, cpu_count);
    return API_ERRCODE_OOM;
}

static INLINE int _api_iface_ipv4_manage_mount(struct ipv4_manage *one, struct ipv4_info *info)
{
    int idx = 0;
    struct ipv4_info *cur = NULL;
    struct list_head *head = NULL;

    idx = L3_IPv4_BUCKET_IDX(info->ip);
    head = &one->head[idx];

    list_for_each_entry(cur, head, node) {
        if (cur->ip == info->ip) {
            LOG_ERROR("IP exists(%#x)", info->ip);
            return API_ERRCODE_IP_EXIST;
        } else if (cur->ip < info->ip) {
            continue;
        } else {
            break;
        }
    }

    list_add_tail(&cur->node, &info->node);

    if (info->type == IP_MASTER && one->info[info->port] == NULL) {
        one->info[info->port] = info;
    }

    return API_ERRCODE_SUCCESS;
}

static enum API_ERRCODE _api_iface_ipv4_manage_add(struct ipv4_manage **ipv4_manage, const struct root *root,
                                                   const struct api_interface *iface, int count)
{
    int ret = 0;
    struct ipv4_info *info = NULL;
    struct ipv4_manage *one = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        int idx = 0;

        one = ipv4_manage[i];

        for (int j = 0; j < count; j++) {
            info = dpdk_malloc_numa(sizeof(*info), root->dpdk_thread[i]->hw_numa_id);
            if (info == NULL) {
                LOG_ERROR("OOM");
                return API_ERRCODE_OOM;
            }

            memset(info, 0, sizeof(*info));

            info->ip = iface[j].ip.ipv4;
            info->mask = iface[j].mask;
            info->port = iface[j].port;
            info->type = iface[j].ip_type;

            INIT_LIST_HEAD(&info->node);

            ret = _api_iface_ipv4_manage_mount(one, info);
            if (ret != 0) {
                return API_ERRCODE_IP_EXIST;
            }
        }

        one->ip_count += count;
    }

    return 0;
}

static enum API_ERRCODE _api_iface_ipv4_manage_del(struct ipv4_manage **ipv4_manage, const struct root *root,
                                                   const struct api_interface *iface, int count)
{
    int ret = 0;
    int idx = 0;
    uint32_t ip = 0;
    int cpu_count = 0;
    struct ipv4_info *cur = NULL;
    struct ipv4_info *next = NULL;
    struct list_head *head = NULL;
    struct ipv4_manage *one = NULL;

    cpu_count = root->hw_info.cpu_count;
    for (int i = 0; i < cpu_count; i++) {
        one = ipv4_manage[i];

        for (int j = 0; j < count; j++) {
            ip = iface[j].ip.ipv4;
            idx = L3_IPv4_BUCKET_IDX(ip);
            head = &one->head[idx];

            list_for_each_entry_safe(cur, next, head, node) {
                if (cur->ip == ip) {
                    list_del_init(&cur->node);
                    if (one->info[cur->port] == cur) {
                        one->info[cur->port] = NULL;
                    }
                    dpdk_free(cur);
                } else if (cur->ip < ip) {
                    continue;
                } else {
                    LOG_ERROR("IP(%u) not exist", ip);
                    return API_ERRCODE_IP_NOT_EXIST;
                }
            }
        }

        one->ip_count -= count;
    }

    return API_ERRCODE_SUCCESS;
}

static INLINE enum API_ERRCODE _api_iface_ipv4_manage(struct root *root, struct ipv4_manage **ipv4_manage,
                                                      struct api_interface *iface, int count, bool is_add)
{
    int i = 0;
    enum API_ERRCODE code = 0;

    code = _api_iface_ipv4_manage_construct(root, ipv4_manage);
    if (code != 0) {
        return code;
    }

    if (is_add) {
        code = _api_iface_ipv4_manage_add(ipv4_manage, root, iface, count);
        if (code != 0) {
            _api_iface_ipv4_manage_destruct(ipv4_manage, root->hw_info.cpu_count);
            return code;
        }
    } else {
        code = _api_iface_ipv4_manage_del(ipv4_manage, root, iface, count);
        if (code != 0) {
            _api_iface_ipv4_manage_destruct(ipv4_manage, root->hw_info.cpu_count);
            return code;
        }
    }

    return API_ERRCODE_SUCCESS;
}

static INLINE void _api_iface_arp_send(struct root *root, void *arp_mbuf[], int count)
{
    dpdk_ring_mp_push(root->dpdk_thread[s_arp_thread_id]->notice_ring, arp_mbuf, count);
}

static INLINE void *_api_iface_update(void *cfg, const char *url, void *json, void *sess)
{
    int count = 0;
    struct root *root = cfg;
    enum API_ERRCODE code = 0;
    void **position[CPU_MAX] = {NULL};
    void *arp[DPDK_ETHPORT_MAX] = {NULL};
    struct ipv4_manage *ipv4_manage[CPU_MAX] = {NULL};
    struct api_interface *api_iface = {0};

    count = _api_iface_post_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(API_ERRCODE_INVALID);
    }

    api_iface = _api_iface_alloc(count);
    if (api_iface == NULL) {
        goto _quit;
    }

    code = _api_iface_post_parse(cfg, json, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_iface_broadcast_gen(cfg, arp, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_iface_ipv4_manage(cfg, ipv4_manage, api_iface, count, true);
    if (code != 0) {
        _api_iface_broadcast_free(arp, count);
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        position[i] = &root->dpdk_thread[i]->tc->ipv4_manage;
    }

    api_config_update(cfg, position, (void **)ipv4_manage, l3_thread_ipv4_destroy);
    _api_iface_arp_send(cfg, arp, count);
    _api_iface_free(api_iface);

    LOG_DEBUG("CONFIG IP SUCCESS.");

    return api_succ(NULL);

_quit:
    if (api_iface != NULL) {
        _api_iface_free(api_iface);
    }

    return api_fail(code);
}

API_POST(/v1/network/interface, interface)
{
    return _api_iface_update(cfg, url, json, sess);
}

API_PUT(/v1/network/interface, interface)
{
    return api_fail(API_ERRCODE_NOT_SUPPORT);
}

API_DELETE(/v1/network/interface, interface)
{
    int count = 0;
    struct root *root = cfg;
    enum API_ERRCODE code = 0;
    void **position[CPU_MAX] = {NULL};
    void *arp[DPDK_ETHPORT_MAX] = {NULL};
    struct ipv4_manage *ipv4_manage[CPU_MAX] = {NULL};
    struct api_interface *api_iface = {0};

    count = _api_iface_del_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(API_ERRCODE_INVALID);
    }

    api_iface = _api_iface_alloc(count);
    if (api_iface == NULL) {
        return api_fail(API_ERRCODE_OOM);
    }

    code = _api_iface_del_parse(cfg, json, api_iface, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_iface_ipv4_manage(cfg, ipv4_manage, api_iface, count, false);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        position[i] = &root->dpdk_thread[i]->tc->ipv4_manage;
    }

    api_config_update(cfg, position, (void **)ipv4_manage, l3_thread_ipv4_destroy);
    _api_iface_free(api_iface);

    return api_succ(NULL);

_quit:
    if (api_iface != NULL) {
        _api_iface_free(api_iface);
    }

    return api_fail(code);
}

API_GET(/v1/network/interface, interface)
{
    int ret = 0;
    void *obj = NULL;
    void *array = NULL;
    size_t val_cnt = 0;
    sr_val_t *val = NULL;
    char path[2 * CACHE_LINE + 1] = {0};
    const struct port_name *port_name = NULL;
    const struct port_name_entry *one = NULL;

    array = json_array();
    if (array == NULL) {
        LOG_ERROR("OOM.");
        return api_fail(API_ERRCODE_INNER);
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

        obj = _api_iface_obj_gen(one->name, val, val_cnt);
        if (obj == NULL) {
            goto _quit;
        }

        sr_free_values(val, val_cnt);

        ret = json_array_append_new(array, obj);
        if (ret != 0) {
            LOG_ERROR("OOM.");
            json_decref(obj);
            goto _quit;
        }
    }

    return api_succ(array);

_quit:
    if (array != NULL) {
        json_decref(array);
    }

    if (val != NULL) {
        sr_free_values(val, val_cnt);
    }

    return api_fail(API_ERRCODE_INNER);
}