/*****************************************************************************
 * filename: api_ip6.c
 * function:
 * description:
 *****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>

#include <jansson.h>
#include <sysrepo.h>

#include "l2.h"
#include "log.h"
#include "type.h"
#include "errcode.h"
#include "ip6_conf.h"
#include "dpdk_ip6.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_common.h"
#include "route6_conf.h"

#define API_IP6_MODULE_NAME "ip6"
#define API_IP6_LIST_NAME "entries"

#define API_INTERFACE_FORMAT "/v1:" API_IP6_MODULE_NAME "/" API_IP6_LIST_NAME "[name='%s']/*"

struct api_param {
    const char *name;
    uint8_t port;
    int mask;
    enum IP_TYPE type;
    struct dpdk_mac mac;
    struct dpdk_ip6_addr addr;
};

struct api_param_hdr {
    int count;
    struct api_param param[];
};

struct api_ip6_hdr {
    int cpu_count;
    int ele_count;

    struct ip6_info *info[CPU_MAX];
    void *ip6_table[CPU_MAX];

    struct route6_item *item[CPU_MAX];
    void *route6_table[CPU_MAX];

    void **ndp;
};

static void _api_ip6_param_hdr_free(struct api_param_hdr *param_hdr)
{
    if (param_hdr == NULL) {
        return;
    }

    api_free(param_hdr);
}

static void _api_ip6_ndp_free(void *mbufs[], int count)
{
    bool has = false;

    if (mbufs == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (mbufs[i] != NULL) {
            has = true;
            break;
        }
    }

    if (has) {
        dpdk_pktmbuf_push(mbufs, count);
        memset(mbufs, 0, count *sizeof(*mbufs));
    }

    api_free(mbufs);
}

static void _api_ip6_hdr_free(struct api_ip6_hdr *ip6_hdr)
{
    if (ip6_hdr == NULL) {
        return;
    }

    for (int i = 0; i < ip6_hdr->cpu_count; i++) {
        if (ip6_hdr->info[i] != NULL) {
            api_free(ip6_hdr->info[i]);
            ip6_hdr->info[i] = NULL;
        }

        if (ip6_hdr->ip6_table[i] != NULL) {
            ip6_conf_table_destroy(ip6_hdr->ip6_table[i]);
            ip6_hdr->ip6_table[i] = NULL;
        }

        if (ip6_hdr->item[i] != NULL) {
            api_free(ip6_hdr->item[i]);
            ip6_hdr->item[i] = NULL;
        }

        if (ip6_hdr->route6_table[i] != NULL) {
            route6_conf_destroy(ip6_hdr->route6_table[i]);
            ip6_hdr->route6_table[i] = NULL;
        }
    }

    _api_ip6_ndp_free(ip6_hdr->ndp, ip6_hdr->ele_count);
    api_free(ip6_hdr);
}

static void *_api_ip6_param_hdr_alloc(int count)
{
    size_t total = 0;
    struct api_param_hdr *param_hdr = NULL;

    total = sizeof(*param_hdr) + count * sizeof(*param_hdr->param);
    param_hdr = api_malloc(total);
    if (param_hdr == NULL) {
        return NULL;
    }

    param_hdr->count = count;
    return param_hdr;
}

static void *_api_ip6_hdr_alloc(void)
{
    struct api_ip6_hdr *ip6_hdr = NULL;

    ip6_hdr = api_malloc(sizeof(*ip6_hdr));
    if (ip6_hdr == NULL) {
        return NULL;
    }

    return ip6_hdr;
}

int ip6_info_init(struct ip6_info *info, const struct dpdk_ip6_addr *addr, uint8_t mask, uint16_t port, enum IP_TYPE type, uint32_t refcnt)
{
    if (info == NULL) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_INNER;
    }

    info->addr = *addr;
    info->mask = mask;
    info->port = port;
    info->type = type;
    info->refcnt = refcnt;

    return 0;
}

static INLINE void _api_ip6_to_subnet(struct dpdk_ip6_addr *addr, const struct dpdk_ip6_addr *ip6, uint8_t mask)
{
    *addr = *ip6;
    dpdk_ip6_addr_subnet(addr, mask);
}

static int _api_ip6_to_route_item(struct route6_item **pp_item, const struct api_param param[], int count)
{
    struct route6_item *item = NULL;
    const struct api_param *one = NULL;

    item = api_malloc(sizeof(*item) * count);
    if (item == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        one = &param[i];

        INIT_LIST_HEAD(&item[i].lru_head);
        dpdk_ip6_addr_unspec(&item[i].nexthop);
        _api_ip6_to_subnet(&item[i].dst_subnet, &one->addr, one->mask);
        item[i].mask = one->mask;
        item[i].route_type = ROUTE6_DIRECT;
        item[i].port = one->port;
        item[i].valid = 1;
    }

    *pp_item = item;
    return 0;
}

static int _api_ip6_param_to_info(struct ip6_info **pp_ip6_info, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = param_hdr->count;
    struct ip6_info *one = NULL;
    struct ip6_info *ip6_info = NULL;
    const struct api_param *param = NULL;

    ip6_info = api_malloc(count * sizeof(*ip6_info));
    if (ip6_info == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        one = &ip6_info[i];

        code = ip6_info_init(one, &param->addr, param->mask, param->port, param->type, 1);
        if (code != 0) {
            api_free(ip6_info);
            return code;
        }
    }

    *pp_ip6_info = ip6_info;
    return 0;
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
                ret = api_json_add_long(obj, "mask", val[j].data.uint8_val);
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
    api_json_free(obj);
    return ERRCODE_OOM;
}

static int _api_ip6_table_create(struct api_ip6_hdr *ip6_hdr, struct root *root, struct api_param_hdr *param_hdr,
                                 int (*table_create_fn)(void **, void *, const struct ip6_info *, int, int))
{
    int code = 0;
    void *table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = ip6_hdr->cpu_count;

    ip6_hdr->ele_count = param_hdr->count;
    for (int i = 0; i < cpu_count; i++) {
        code = _api_ip6_param_to_info(&ip6_hdr->info[i], param_hdr);
        if (code != 0) {
            return code;
        }

        dp = root->dpdk_thread[i];
        code = table_create_fn(&table, dp->tc->ip6_table, ip6_hdr->info[i], ip6_hdr->ele_count, dp->hw_numa_id);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_ip6_route_table_del_create(struct api_ip6_hdr *ip6_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = 0;
    int hw_numa_id = 0;
    void *route6_table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = ip6_hdr->cpu_count;

    count = param_hdr->count;
    for (int i = 0; i < cpu_count; i++) {
        code = _api_ip6_to_route_item(&ip6_hdr->item[i], param_hdr->param, param_hdr->count);
        if (code != 0) {
            return code;
        }

        dp = root->dpdk_thread[i];
        hw_numa_id = dp->hw_numa_id;
        route6_table = dp->tc->route6_table;

        code = route6_conf_create_and_delete(&ip6_hdr->route6_table[i], route6_table, ip6_hdr->item[i], count, hw_numa_id, false);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_ip6_route_table_create(struct api_ip6_hdr *ip6_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = 0;
    int hw_numa_id = 0;
    void *ip6_table = NULL;
    void *route6_table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = ip6_hdr->cpu_count;

    count = param_hdr->count;
    for (int i = 0; i < cpu_count; i++) {
        code = _api_ip6_to_route_item(&ip6_hdr->item[i], param_hdr->param, param_hdr->count);
        if (code != 0) {
            return code;
        }

        dp = root->dpdk_thread[i];
        hw_numa_id = dp->hw_numa_id;
        ip6_table = ip6_hdr->ip6_table[i];
        route6_table = dp->tc->route6_table;

        code = route6_conf_create_and_append(&ip6_hdr->route6_table[i], route6_table, ip6_hdr->item[i], count, hw_numa_id, ip6_table);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_ip6_ndp_create(struct api_ip6_hdr *ip6_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct dataplane *dp = NULL;
    int count = ip6_hdr->ele_count;
    struct api_param *param = NULL;
    int cpu_count = root->hw_info.cpu_count;

    ip6_hdr->ndp = api_malloc(param_hdr->count * sizeof(*ip6_hdr->ndp));
    if (ip6_hdr->ndp == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = dpdk_pktmbuf_pop(dp->pktmbuf_pool, ip6_hdr->ndp, count);
        if (code == 0) {
            break;
        }
    }

    if (code < 0) {
        _api_ip6_ndp_free(ip6_hdr->ndp, count);
        ip6_hdr->ndp = NULL;
        return ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        code = ip6_ndp_na_mcast_gen(ip6_hdr->ndp[i], param->port, &param->addr, &param->mac);
        if (code != 0) {
            _api_ip6_ndp_free(ip6_hdr->ndp, count);
            ip6_hdr->ndp = NULL;
            return code;
        }

        DPDK_HEADROOM(ip6_hdr->ndp[i])->type = PKT_MBUF_NDP_AD;
    }

    return 0;
}

static int _api_ip6_del_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    uint64_t lvalue = 0;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_delete_list(&array, &count, json, API_IP6_MODULE_NAME, API_IP6_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    param_hdr = _api_ip6_param_hdr_alloc(count);
    if (param_hdr == NULL) {
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
        if (code < 0) {
            code = ERRCODE_INNER;
            goto _quit;
        }

        svalue = api_json_get_string(obj, "addr");
        if (svalue == NULL) {
            goto _quit;
        }

        inet_pton(AF_INET6, svalue, &param->addr);

        code = api_json_get_long(&lvalue, obj, "mask");
        if (code != 0) {
            goto _quit;
        }

        param->mask = (int) lvalue;
        param->type = 0;
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_ip6_param_hdr_free(param_hdr);
    return code;
}

static int _api_ip6_post_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    uint64_t lvalue = 0;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_modify_list(&array, &count, json, API_IP6_MODULE_NAME, API_IP6_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    param_hdr = _api_ip6_param_hdr_alloc(count);
    if (param_hdr == NULL) {
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

        inet_pton(AF_INET6, svalue, &param->addr);

        code = api_json_get_long(&lvalue, obj, "mask");
        if (code != 0) {
            goto _quit;
        }

        param->mask = (uint16_t) lvalue;
        param->type = 0;
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_ip6_param_hdr_free(param_hdr);
    return code;
}

static int _api_ip6_del(struct api_ip6_hdr **pp_ip6_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = param_hdr->count;
    struct api_ip6_hdr *ip6_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    ip6_hdr = _api_ip6_hdr_alloc();
    if (ip6_hdr == NULL) {
        return ERRCODE_OOM;
    }

    ip6_hdr->ele_count = count;
    ip6_hdr->cpu_count = cpu_count;

    code = _api_ip6_table_create(ip6_hdr, root, param_hdr, ip6_conf_table_create_and_delete);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_route_table_del_create(ip6_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    *pp_ip6_hdr = ip6_hdr;
    return 0;

_quit:
    _api_ip6_hdr_free(ip6_hdr);
    return code;
}

static int _api_ip6_add(struct api_ip6_hdr **pp_ip6_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    int count = param_hdr->count;
    struct api_ip6_hdr *ip6_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    ip6_hdr = _api_ip6_hdr_alloc();
    if (ip6_hdr == NULL) {
        return ERRCODE_OOM;
    }

    ip6_hdr->cpu_count = count;
    ip6_hdr->cpu_count = cpu_count;

    code = _api_ip6_table_create(ip6_hdr, root, param_hdr, ip6_conf_table_create_and_append);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_route_table_create(ip6_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_ndp_create(ip6_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    *pp_ip6_hdr = ip6_hdr;
    return 0;

_quit:
    _api_ip6_hdr_free(ip6_hdr);
    return code;
}

static void _api_ip6_update(struct api_ip6_hdr *ip6_hdr, struct root *root)
{
    struct dataplane *dp = NULL;
    void **position[CPU_MAX] = {NULL};
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->ip6_table;
    }

    api_thread_config_update(root, position, (void **)ip6_hdr->ip6_table, ip6_conf_table_destroy);

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = (void **)&dp->tc->route6_table;
    }

    api_thread_config_update(root, position, (void **)ip6_hdr->route6_table, route6_conf_destroy);

    dp = root->dpdk_thread[0];
    if (ip6_hdr->ndp != NULL) {
        dpdk_ring_mp_push(dp->notice_ring, ip6_hdr->ndp, ip6_hdr->ele_count);
        memset(ip6_hdr->ndp, 0, ip6_hdr->ele_count * sizeof(*ip6_hdr->ndp));
    }
}

API_POST(/v1/network/ip6, ip6)
{
    int code = 0;
    struct root *root = cfg;
    struct api_ip6_hdr *ip6_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_ip6_post_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_add(&ip6_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_ip6_update(ip6_hdr, root);

_quit:
    _api_ip6_param_hdr_free(param_hdr);
    _api_ip6_hdr_free(ip6_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_PUT(/v1/network/ip6, ip6)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/ip6, ip6)
{
    int code = 0;
    struct root *root = cfg;
    struct api_ip6_hdr *ip6_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_ip6_del_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_del(&ip6_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_ip6_update(ip6_hdr, root);

_quit:
    _api_ip6_param_hdr_free(param_hdr);
    _api_ip6_hdr_free(ip6_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_GET(/v1/network/ip6, ip6)
{
    int code = 0;
    size_t val_cnt = 0;
    void *array = NULL;
    sr_val_t *val = NULL;
    char path[2 * CACHE_LINE + 1] = "";
    const struct port_info *port_info = NULL;
    const struct port_info_entry *one = NULL;

    code = api_json_array(&array);
    if (code != 0) {
        goto _quit;
    }

    port_info = dpdk_port_info_get();
    for (int i = 0; i < port_info->count; i++) {
        one = &port_info->info[i];

        snprintf(path, sizeof(path), API_INTERFACE_FORMAT, one->name);
        code = sr_get_items(sess, path, 0, 0, &val, &val_cnt);
        if (code != 0 && code != SR_ERR_NOT_FOUND) {
            LOG_ERROR("Failure path(%s) sr_get_item: %s", path, strerror(code));
            goto _quit;
        }

        code = _api_ip6_obj_gen(array, val, val_cnt);
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
    return api_fail(code);
}