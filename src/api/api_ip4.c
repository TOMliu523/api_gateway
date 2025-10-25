/*****************************************************************************
 * filename: api_ip4.c
 * function:
 * description:
 ****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>

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

#define API_IP4_MODULE_NAME "ip4"
#define API_IP4_LIST_NAME "entries"

#define API_INTERFACE_FORMAT "/v1:" API_IP4_MODULE_NAME "/" API_IP4_LIST_NAME "[name='%s']/*"

struct api_param {
    const char *name;
    uint8_t port;
    int mask;
    enum IP_TYPE ip_type;
    uint32_t addr;
    struct dpdk_mac mac;
};

struct api_param_hdr {
    int count;
    struct api_param param[];
};

struct api_ip4_hdr {
    int cpu_count;
    int ele_count;
    struct ip4_info *info[CPU_MAX];
    void *ip4_table[CPU_MAX];

    struct route4_item *item[CPU_MAX];
    void *route4_table[CPU_MAX];

    void **arp;
};

static void _api_ip4_arp_free(void **arp, int count)
{
    bool has = false;

    if (arp == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (arp[i] != NULL) {
            has = true;
            break;
        }
    }

    if (has) {
        dpdk_pktmbuf_push(arp, count);
        for (int i = 0; i < count; i++) {
            arp[i] = NULL;
        }
    }

    api_free(arp);
}

static void _api_ip4_hdr_free(struct api_ip4_hdr *ip4_hdr)
{
    if (ip4_hdr == NULL) {
        return;
    }

    for (int i = 0; i < ip4_hdr->cpu_count; i++) {
        if (ip4_hdr->info[i] != NULL) {
            api_free(ip4_hdr->info[i]);
            ip4_hdr->info[i] = NULL;
        }

        if (ip4_hdr->ip4_table[i] != NULL) {
            ip4_conf_table_destroy(ip4_hdr->ip4_table[i]);
            ip4_hdr->ip4_table[i] = NULL;
        }

        if (ip4_hdr->item[i] != NULL) {
            api_free(ip4_hdr->item[i]);
            ip4_hdr->item[i] = NULL;
        }

        if (ip4_hdr->route4_table[i] != NULL) {
            route4_conf_destroy(ip4_hdr->route4_table[i]);
            ip4_hdr->route4_table[i] = NULL;
        }
    }

    _api_ip4_arp_free(ip4_hdr->arp, ip4_hdr->ele_count);
    ip4_hdr->arp = NULL;

    api_free(ip4_hdr);
}

static void _api_ip4_param_hdr_free(struct api_param_hdr *param_hdr)
{
    if (param_hdr == NULL) {
        return;
    }

    api_free(param_hdr);
}

static void *_api_ip4_param_hdr_alloc(size_t count)
{
    size_t total = 0;
    struct api_param_hdr *param_hdr = NULL;

    total = sizeof(*param_hdr) + count * sizeof(struct api_param);
    param_hdr = api_malloc(total);
    if (param_hdr == NULL) {
        return NULL;
    }

    param_hdr->count = count;
    return param_hdr;
}

static void *_api_ip4_hdr_alloc(int cpu_count)
{
    struct api_ip4_hdr *ip4_hdr = NULL;

    ip4_hdr = api_malloc(sizeof(struct api_ip4_hdr));
    if (ip4_hdr == NULL) {
        return NULL;
    }

    ip4_hdr->cpu_count = cpu_count;
    return ip4_hdr;
}

static int _api_ip4_obj_gen(void *array, sr_val_t *val, size_t val_cnt)
{
    int i = 0;
    int j = 0;
    int m = 0;
    int ret = 0;
    int code = 0;
    int group = 0;
    void *obj = NULL;
    const int ele_cnt = 4;

    group = val_cnt / ele_cnt;
    for (i = 0; i < group; i++) {
        obj = NULL;
        code = api_json_object(&obj);
        if (code != 0) {
            goto _quit;
        }

        m = j + ele_cnt;
        for (; j < m; j++) {
            size_t len = 0;
            const char *xpath = val[j].xpath;

            len = strlen(xpath);
            if (len >= 5 && strcmp(xpath + len - 5, "/addr") == 0) {
                ret = api_json_add_string(obj, "addr", val[j].data.string_val);
            } else if (len >= 5 && strcmp(xpath + len - 5, "/mask") == 0) {
                ret = api_json_add_long(obj, "mask", val[j].data.uint8_val);
            } else if (len >= 5 && strcmp(xpath + len - 5, "/type") == 0) {
                ret = api_json_add_string(obj, "type", val[j].data.string_val);
            } else if (len >= 5 && strcmp(xpath + len - 5, "/name") == 0) {
                ret = api_json_add_string(obj, "name", val[j].data.string_val);
            } else {
                LOG_ERROR("Not exist element '%s'.", xpath);
                goto _quit;
            }

            if (ret != 0) {
                goto _quit;
            }
        }

        code = api_json_array_append(array, obj);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    api_json_free(obj);
    return code;
}

static int _api_ip4_del_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    uint64_t lvalue = 0;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_delete_list(&array, &count, json, API_IP4_MODULE_NAME, API_IP4_LIST_NAME);
    if (code != 0) {
        return code;
    }

    param_hdr = _api_ip4_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (int i = 0; i < count; i++) {
        param = &param_hdr->param[i];

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            goto _quit;
        }

        param->name = api_json_get_string(obj, "interface-name");
        if (param->name == NULL) {
            goto _quit;
        }

        param->port = dpdk_port_by_name_get(param->name);
        if (param->port == UINT8_MAX) {
            goto _quit;
        }

        code = l2_conf_port_mac(param->port, &param->mac);
        if (code != 0) {
            goto _quit;
        }

        svalue = api_json_get_string(obj, "addr");
        if (svalue == NULL) {
            goto _quit;
        }

        inet_pton(AF_INET, svalue, &param->addr);

        code = api_json_get_long(&lvalue, obj, "mask");
        if (code != 0) {
            goto _quit;
        }

        param->mask = (uint8_t)lvalue;
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_ip4_param_hdr_free(param_hdr);
    return code;
}

static int _api_ip4_post_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    void *array = NULL;
    uint64_t lvalue = 0;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_modify_list(&array, &count, json, API_IP4_MODULE_NAME, API_IP4_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    param_hdr = _api_ip4_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        goto _quit;
    }

    code = ERRCODE_PARAMETER_INVALID;
    for (size_t i = 0; i < count; i++) {
        param = &param_hdr->param[i];

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            LOG_ERROR("Invalid parameter");
            goto _quit;
        }

        param->name = api_json_get_string(obj, "interface-name");
        if (param->name == NULL) {
            goto _quit;
        }

        param->port = dpdk_port_by_name_get(param->name);
        if (param->port == UINT8_MAX) {
            goto _quit;
        }

        if (!dpdk_port_is_up(param->port)) {
            LOG_ERROR("Port '%s' is down.", param->name);
            goto _quit;
        }

        code = l2_conf_port_mac(param->port, &param->mac);
        if (code < 0) {
            code = ERRCODE_INNER;
            goto _quit;
        }

        svalue = api_json_get_string(obj, "addr");
        if (svalue == NULL) {
            goto _quit;
        }

        inet_pton(AF_INET, svalue, &param->addr);

        code = api_json_get_long(&lvalue, obj, "mask");
        if (code != 0) {
            goto _quit;
        }

        param->mask = (uint16_t) lvalue;
        param->ip_type = 0;
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_ip4_param_hdr_free(param_hdr);
    return code;
}

int ip4_info_init(struct ip4_info *info, uint32_t addr, uint8_t mask, uint16_t port, enum IP_TYPE type, uint32_t refcnt)
{
    if (info == NULL) {
        LOG_ERROR("Inner parameter invalid.");
        return ERRCODE_INNER;
    }

    info->addr = addr;
    info->mask = mask;
    INIT_LIST_HEAD(&info->node);
    info->port = port;
    info->type = type;
    info->refcnt = refcnt;

    return 0;
}

static int _api_ip4_param_to_info(struct ip4_info **pp_ip4_info, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = param_hdr->count;
    struct ip4_info *one = NULL;
    struct ip4_info *ip4_info = NULL;
    const struct api_param *param = NULL;

    ip4_info = api_malloc(count * sizeof(*ip4_info));
    if (ip4_info == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        one = &ip4_info[i];

        code = ip4_info_init(one, param->addr, param->mask, param->port, param->ip_type, 1);
        if (code != 0) {
            api_free(ip4_info);
            return code;
        }
    }

    *pp_ip4_info = ip4_info;
    return 0;
}

static INLINE uint32_t _api_ip4_to_subnet(uint32_t addr_be, int mask)
{
    uint32_t addr = dpdk_be_to_cpu_32(addr_be);
    uint32_t addr_mask = L3_MASK_TO_IP(mask);

    return dpdk_cpu_to_be_32(addr & addr_mask);
}

static int _api_ip4_to_route_item(struct route4_item **pp_item, const struct api_param params[], int count)
{
    struct route4_item *one = NULL;
    struct route4_item *items = NULL;
    const struct api_param *param = NULL;

    items = api_malloc(count * sizeof(*items));
    if (items == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        param = &params[i];
        one = &items[i];

        INIT_LIST_HEAD(&one->lru_head);
        one->nexthop = 0;
        one->dst_subnet = _api_ip4_to_subnet(param->addr, param->mask);
        one->mask = param->mask;
        one->route_type = ROUTE4_DIRECT;
        one->priority = 0;
        one->port = param->port;
        one->last_access_time = 0;
        one->last_probe_time = 0;
        one->valid = 1;
        one->direct_id = 0;
    }

    return 0;
}

static int _api_ip4_table_create(struct api_ip4_hdr *ip4_hdr, struct root *root, const struct api_param_hdr *param_hdr,
                                 int (*table_create_fn)(void **, void *, const struct ip4_info *, int, int))
{
    int code = 0;
    void *table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = ip4_hdr->cpu_count;

    ip4_hdr->ele_count = param_hdr->count;
    for (int i = 0; i < cpu_count; i++) {
        code = _api_ip4_param_to_info(&ip4_hdr->info[i], param_hdr);
        if (code != 0) {
            return code;
        }

        dp = root->dpdk_thread[i];
        code = table_create_fn(&table, dp->tc->ip4_table, ip4_hdr->info[i], ip4_hdr->ele_count, dp->hw_numa_id);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_ip4_route_table_create(struct api_ip4_hdr *ip4_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = 0;
    int hw_numa_id = 0;
    void *ip4_table = NULL;
    struct dataplane *dp = NULL;
    struct proto_header *proto = NULL;
    int cpu_count = ip4_hdr->cpu_count;

    count = param_hdr->count;
    for (int i = 0; i < cpu_count; i++) {
        code = _api_ip4_to_route_item(&ip4_hdr->item[i], param_hdr->param, param_hdr->count);
        if (code != 0) {
            return code;
        }

        dp = root->dpdk_thread[i];
        proto = dp->protocol;
        hw_numa_id = dp->hw_numa_id;
        ip4_table = ip4_hdr->ip4_table[i];

        code = route4_conf_create_and_append(&ip4_hdr->route4_table[i], proto->route4, ip4_hdr->item[i], count, hw_numa_id, ip4_table);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_ip4_route_table_delete(struct api_ip4_hdr *ip4_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = 0;
    int hw_numa_id = 0;
    struct dataplane *dp = NULL;
    struct proto_header *proto = NULL;
    int cpu_count = ip4_hdr->cpu_count;

    count = param_hdr->count;
    for (int i = 0; i < cpu_count; i++) {
        code = _api_ip4_to_route_item(&ip4_hdr->item[i], param_hdr->param, param_hdr->count);
        if (code != 0) {
            return code;
        }

        dp = root->dpdk_thread[i];
        proto = dp->protocol;
        hw_numa_id = dp->hw_numa_id;

        code = route4_conf_create_and_delete(&ip4_hdr->route4_table[i], proto->route4, ip4_hdr->item[i], count, hw_numa_id, false);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_ip4_arp_create(struct api_ip4_hdr *ip4_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int ret = 0;
    int code = 0;
    struct dataplane *dp = NULL;
    int count = param_hdr->count;
    const struct api_param *param = NULL;
    int cpu_count = root->hw_info.cpu_count;

    ip4_hdr->arp = api_malloc(param_hdr->count * sizeof(*ip4_hdr->arp));
    if (ip4_hdr->arp == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        ret = dpdk_pktmbuf_pop(dp->pktmbuf_pool, ip4_hdr->arp, count);
        if (ret == 0) {
            break;
        }
    }

    if (ret < 0) {
        _api_ip4_arp_free(ip4_hdr->arp, ip4_hdr->ele_count);
        ip4_hdr->arp = NULL;
        return ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        code = l2_gratuitous_arp_gen(ip4_hdr->arp[i], param->port, param->addr, &param->mac);
        if (code != 0) {
            _api_ip4_arp_free(ip4_hdr->arp, ip4_hdr->ele_count);
            ip4_hdr->arp = NULL;
            return code;
        }

        DPDK_HEADROOM(ip4_hdr->arp[i])->type = PKT_MBUF_GARP;
    }

    return 0;
}

static int _api_ip4_del(struct api_ip4_hdr **pp_ip4_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_ip4_hdr *ip4_hdr = NULL;

    ip4_hdr = _api_ip4_hdr_alloc(root->hw_info.cpu_count);
    if (ip4_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = _api_ip4_table_create(ip4_hdr, root, param_hdr, ip4_conf_table_create_and_delete);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_route_table_delete(ip4_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    *pp_ip4_hdr = ip4_hdr;
    return 0;

_quit:
    _api_ip4_hdr_free(ip4_hdr);
    return code;
}

static int _api_ip4_add(struct api_ip4_hdr **pp_ip4_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_ip4_hdr *ip4_hdr = NULL;

    ip4_hdr = _api_ip4_hdr_alloc(root->hw_info.cpu_count);
    if (ip4_hdr == NULL) {
        goto _quit;
    }

    code = _api_ip4_table_create(ip4_hdr, root, param_hdr, ip4_conf_table_create_and_append);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_route_table_create(ip4_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_arp_create(ip4_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    *pp_ip4_hdr = ip4_hdr;
    return 0;

_quit:
    _api_ip4_hdr_free(ip4_hdr);
    return code;
}

static void _api_ip4_update(struct api_ip4_hdr *ip4_hdr, struct root *root)
{
    struct dataplane *dp = NULL;
    void **position[CPU_MAX] = {NULL};
    struct proto_header *protocol = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->ip4_table;
    }

    api_thread_config_update(root, position, ip4_hdr->ip4_table, ip4_conf_table_destroy);

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        protocol = dp->protocol;
        position[i] = (void **)&protocol->route4;
    }

    api_thread_config_update(root, position, ip4_hdr->route4_table, route4_conf_destroy);

    dp = root->dpdk_thread[0];
    dpdk_ring_mp_push(dp->notice_ring, ip4_hdr->arp, ip4_hdr->ele_count);
    for (int i = 0; i < ip4_hdr->ele_count; i++) {
        ip4_hdr->arp[i] = NULL;
    }
}

API_POST(/v1/network/ip4, ip4)
{
    int code = 0;
    struct root *root = cfg;
    struct api_ip4_hdr *ip4_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_ip4_post_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_add(&ip4_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_ip4_update(ip4_hdr, root);

_quit:
    _api_ip4_hdr_free(ip4_hdr);
    _api_ip4_param_hdr_free(param_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_PUT(/v1/network/ip4, ip4)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/ip4, ip4)
{
    int code = 0;
    struct root *root = cfg;
    struct api_ip4_hdr *ip4_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_ip4_del_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip4_del(&ip4_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_ip4_update(ip4_hdr, root);

_quit:
    _api_ip4_hdr_free(ip4_hdr);
    _api_ip4_param_hdr_free(param_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_GET(/v1/network/ip4, ip4)
{
    int code = 0;
    void *array = NULL;
    size_t val_cnt = 0;
    sr_val_t *val = NULL;
    char path[2 * CACHE_LINE + 1] = {0};
    const struct port_info_entry *one = NULL;
    const struct port_info *port_info = NULL;

    code = api_json_array(&array);
    if (code != 0) {
        return api_fail(code);
    }

    port_info = dpdk_port_info_get();
    for (int i = 0; i < port_info->count; i++) {
        one = &port_info->info[i];

        snprintf(path, sizeof(path), API_INTERFACE_FORMAT, one->name);
        code = sr_get_items(sess, path, 0, 0, &val, &val_cnt);
        if (code != 0 && code != SR_ERR_NOT_FOUND) {
            LOG_ERROR("Failure path '%s' sr_get_item: %s", path, strerror(code));
            goto _quit;
        }

        code = _api_ip4_obj_gen(array, val, val_cnt);
        if (code != 0) {
            goto _quit;
        }

        sr_free_values(val, val_cnt);
    }

    return api_succ(NULL);

_quit:
    api_json_free(array);
    if (val != NULL) {
        sr_free_values(val, val_cnt);
    }
    return api_fail(ERRCODE_INNER);
}