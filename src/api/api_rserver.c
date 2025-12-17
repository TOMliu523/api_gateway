/*****************************************************************************
 * filename: api_rserver.c
 * function:
 * description:
 ****************************************************************************/

#include <arpa/inet.h>

#include "log.h"
#include "conf.h"
#include "type.h"
#include "errcode.h"
#include "api_inner.h"
#include "dpdk_common.h"
#include "rserver_conf.h"

#define API_RS_LIST_NAME "entries"
#define API_RS_MODULE_NAME "rserver"

struct api_param {
    int af;
    uint16_t port;
    union inet_addr addr;
};

struct api_param_hdr {
    int count;
    struct api_param param[];
};

struct api_vs_hdr {
    int cpu_count;
    int ele_count;

    bool is_delete;

    struct rserver **rs[CPU_MAX];
    void *rs_table[CPU_MAX];
};

static void _api_rs_param_hdr_free(struct api_param_hdr *param_hdr)
{
    if (param_hdr == NULL) {
        return;
    }

    api_free(param_hdr);
}

static void _api_rs_hdr_free(struct api_vs_hdr *vs_hdr)
{
    if (vs_hdr == NULL) {
        return;
    }

    for (int i = 0; i < vs_hdr->cpu_count; i++) {
        struct rserver **rss = vs_hdr->rs[i];
        if (rss == NULL) {
            continue;
        }

        if (vs_hdr->is_delete) {
            for (int j = 0; j < vs_hdr->ele_count; j++) {
                if (rss[j] != NULL) {
                    rs_conf_free(rss[j]);
                    rss[j] = NULL;
                }
            }
        }

        api_free(rss);
        vs_hdr->rs[i] = NULL;

        if (vs_hdr->rs_table[i] != NULL) {
            rs_conf_table_destroy(vs_hdr->rs_table[i]);
            vs_hdr->rs_table[i] = NULL;
        }
    }
}

static void *_api_rs_hdr_alloc(int cpu_count, int ele_count, bool flags)
{
    struct api_vs_hdr *vs_hdr = NULL;

    vs_hdr = api_malloc(sizeof(*vs_hdr));
    if (vs_hdr == NULL) {
        return NULL;
    }

    for (int i = 0; i < cpu_count; i++) {
        vs_hdr->rs[i] = api_malloc(ele_count * sizeof(*vs_hdr->rs[i]));
        if (vs_hdr->rs[i] == NULL) {
            _api_rs_hdr_free(vs_hdr);
            return NULL;
        }
    }

    vs_hdr->is_delete = flags;
    vs_hdr->cpu_count = cpu_count;
    vs_hdr->ele_count = ele_count;

    return vs_hdr;
}

static void *_api_rs_param_hdr_alloc(int count)
{
    struct api_param_hdr *param_hdr = NULL;

    param_hdr = api_malloc(sizeof(*param_hdr) + count * sizeof(struct api_param));
    if (param_hdr == NULL) {
        return NULL;
    }

    param_hdr->count = count;
    return param_hdr;
}

static int _api_rs_add_check(struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct rserver *rs = NULL;
    const struct api_param *param = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        code = rs_conf_get_by_key(&rs, dp->tc->rs_table, param->af, &param->addr, param->port);
        switch (code) {
        case 0:
            LOG_ERROR("Real server exist");
            return ERRCODE_RSERVER_DUPLICATE;
        case ERRCODE_RSERVER_NOT_FOUND: break;
        default: return ERRCODE_INNER;
        }
    }

    return 0;
}

static int _api_rs_info_get(void **ptr, const struct rserver *rs)
{
    int ret = 0;
    json_t *obj = NULL;
    const struct rserver4 *v4 = NULL;
    const struct rserver6 *v6 = NULL;
    const struct rserver_base *base = NULL;

    obj = json_object();
    if (obj == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    if (rs->af == AF_INET) {
        v4 = (const struct rserver4 *)rs;
        ret = api_json_add_string(obj, "ip", conf_ip4_to_str(v4->addr));
        if (ret != 0) {
            goto _quit;
        }

        base = &v4->base;
    } else {
        v6 = (const struct rserver6 *)rs;
        ret = api_json_add_string(obj, "ip", conf_ip6_to_str(&v6->addr));
        if (ret != 0) {
            goto _quit;
        }

        base = &v6->base;
    }

    ret = api_json_add_long(obj, "port", dpdk_be_to_cpu_16(base->port));
    if (ret != 0) {
        goto _quit;
    }

    *ptr = obj;
    return 0;

_quit:
    json_decref(obj);
    return ERRCODE_OOM;
}

static int _api_rs_post_parse(struct api_param_hdr **pp_param_hdr, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    uint64_t port = 0;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_modify_list(&array, &count, json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (code != 0) {
        return code;
    }

    param_hdr = _api_rs_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (size_t i = 0; i < count; i++) {
        param = &param_hdr->param[i];

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            goto _quit;
        }

        svalue = api_json_get_string(obj, "addr");
        if (svalue == NULL) {
            goto _quit;
        }

        if (strchr(svalue, ':') == NULL) {
            param->af = AF_INET;
            inet_pton(AF_INET, svalue, &param->addr);
        } else {
            param->af = AF_INET6;
            inet_pton(AF_INET6, svalue, &param->addr);
        }

        code = api_json_get_long(&port, obj, "port");
        if (code != 0) {
            goto _quit;
        }

        param->port = dpdk_cpu_to_be_16((uint16_t)port);
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_rs_param_hdr_free(param_hdr);
    return code;
}

static int _api_rs_del_parse(struct api_param_hdr **pp_param_hdr, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    uint64_t port = 0;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_delete_list(&array, &count, json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (code != 0) {
        return code;
    }

    param_hdr = _api_rs_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (size_t i = 0; i < count; i++) {
        param = &param_hdr->param[i];

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            goto _quit;
        }

        svalue = api_json_get_string(obj, "addr");
        if (svalue == NULL) {
            goto _quit;
        }

        if (strchr(svalue, ':') == NULL) {
            param->af = AF_INET;
            inet_pton(AF_INET, svalue, &param->addr);
        } else {
            param->af = AF_INET6;
            inet_pton(AF_INET6, svalue, &param->addr);
        }

        code = api_json_get_long(&port, obj, "port");
        if (code != 0) {
            goto _quit;
        }

        param->port = dpdk_cpu_to_be_16((uint16_t)port);
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_rs_param_hdr_free(param_hdr);
    return code;
}

static int _api_rs_del(struct api_vs_hdr **pp_vs_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct rserver *rs = NULL;
    struct dataplane *dp = NULL;
    struct rserver4 *v4 = NULL;
    struct rserver6 *v6 = NULL;
    struct api_param *param = NULL;
    struct api_vs_hdr *vs_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    vs_hdr = _api_rs_hdr_alloc(cpu_count, param_hdr->count, false);
    if (vs_hdr == NULL) {
        code= ERRCODE_OOM;
        goto _quit;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];

        for (int j = 0; j < param_hdr->count; j++) {
            param = &param_hdr->param[j];

            code = rs_conf_get_by_key(&rs, dp->tc->rs_table, param->af, &param->addr, param->port);
            if (code != 0) {
                goto _quit;
            }

            if (rs->af == AF_INET) {
                v4 = (struct rserver4 *) rs;
                if (v4->base.mtb->pool_refcnt != 0) {
                    LOG_ERROR("Resource busy.");
                    code = ERRCODE_RESOURCE_BUSY;
                    goto _quit;
                }
            } else {
                v6 = (struct rserver6 *) rs;
                if (v6->base.mtb->pool_refcnt != 0) {
                    LOG_ERROR("Resource busy.");
                    code = ERRCODE_RESOURCE_BUSY;
                    goto _quit;
                }
            }

            vs_hdr->rs[i][j] = rs;
        }

        code = rs_conf_table_create_and_delete(&vs_hdr->rs_table[i], dp->tc->rs_table, vs_hdr->rs[i], vs_hdr->ele_count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    vs_hdr->is_delete = true;
    *pp_vs_hdr = vs_hdr;

    return 0;

_quit:
    _api_rs_hdr_free(vs_hdr);
    return code;
}

static int _api_rs_add(struct api_vs_hdr **pp_vs_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct dataplane *dp = NULL;
    struct rserver4 *v4 = NULL;
    struct rserver6 *v6 = NULL;
    struct api_param *param = NULL;
    struct api_vs_hdr *vs_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    code = _api_rs_add_check(root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    vs_hdr = _api_rs_hdr_alloc(cpu_count, param_hdr->count, true);
    if (vs_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];

        for (int j = 0; j < param_hdr->count; j++) {
            param = &param_hdr->param[j];

            if (param->af == AF_INET) {
                v4 = rs_conf_v4_alloc(dp->hw_numa_id);
                if (v4 == NULL) {
                    code = ERRCODE_OOM;
                    goto _quit;
                }

                v4->rs.af = param->af;
                v4->addr = param->addr.ip;
                v4->base.port = param->port;
                v4->base.mtb->pool_refcnt = 0;
                v4->base.mtb->status = RSERVER_ONLINE;
                v4->base.stat->refcnt = 0;

                vs_hdr->rs[i][j] = &v4->rs;
            } else {
                v6 = rs_conf_v6_alloc(dp->hw_numa_id);
                if (v6 == NULL) {
                    code = ERRCODE_OOM;
                    goto _quit;
                }

                v6->rs.af = param->af;
                v6->addr = param->addr.addr;
                v6->base.port = param->port;
                v6->base.mtb->pool_refcnt = 0;
                v6->base.mtb->status = RSERVER_ONLINE;
                v6->base.stat->refcnt = 0;

                vs_hdr->rs[i][j] = &v6->rs;
            }
        }

        code = rs_conf_table_create_and_append(&vs_hdr->rs_table[i], dp->tc->rs_table, vs_hdr->rs[i], param_hdr->count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    vs_hdr->is_delete = false;
    *pp_vs_hdr = vs_hdr;
    return 0;

_quit:
    _api_rs_hdr_free(vs_hdr);
    return code;
}

static void _api_vs_update(struct api_vs_hdr *vs_hdr, struct root *root)
{
    struct dataplane *dp = NULL;
    void **position[CPU_MAX] = {NULL};
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->rs_table;
    }

    api_thread_config_update(root, position, vs_hdr->rs_table, rs_conf_table_destroy);
}

API_POST(/v1/app/rserver, rserver)
{
    int code = 0;
    struct root *root = cfg;
    struct api_vs_hdr *vs_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_rs_post_parse(&param_hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_rs_add(&vs_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_vs_update(vs_hdr, root);

_quit:
    _api_rs_param_hdr_free(param_hdr);
    _api_rs_hdr_free(vs_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

/*
 * Currently there are not enough fields available for modification,
 * so this feature is temporarily not supported.
 */
API_PUT(/v1/app/rserver, rserver)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/app/rserver, rserver)
{
    int code = 0;
    struct root *root = cfg;
    struct api_vs_hdr *vs_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_rs_del_parse(&param_hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_rs_del(&vs_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_vs_update(vs_hdr, root);

_quit:
    _api_rs_param_hdr_free(param_hdr);
    _api_rs_hdr_free(vs_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_GET(/v1/app/rserver, rserver)
{
    int code = 0;
    int count = 0;
    void *obj = NULL;
    void *array = NULL;
    struct root *root = cfg;
    struct rserver **rs_array = NULL;
    struct thread_config *tc = root->dpdk_thread[0]->tc;

    code = api_json_array(&array);
    if (code != 0) {
        return api_fail(code);
    }

    code = rs_conf_table_get_count(&count, tc->rs_table);
    if (code != 0) {
        return api_fail(code);
    }

    if (count == 0) {
        return api_succ(array);
    }

    rs_array = api_malloc(sizeof(*rs_array) * count);
    if (rs_array == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = rs_conf_table_get_element(tc->rs_table, rs_array, &count);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < count; i++) {
        code = _api_rs_info_get(&obj, rs_array[i]);
        if (code != 0) {
            goto _quit;
        }

        if (json_array_append_new(array, obj) != 0) {
            code = ERRCODE_OOM;
            goto _quit;
        }
    }

    api_free(rs_array);
    return api_succ(array);

_quit:
    api_free(rs_array);
    json_decref(array);
    return api_fail(code);
}