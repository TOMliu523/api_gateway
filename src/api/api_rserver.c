/*****************************************************************************
 * filename: api_rserver.c
 * function:
 * description:
 ****************************************************************************/

#include <stdio.h>
#include <arpa/inet.h>
#include <linux/netfilter.h>

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "api_inner.h"
#include "dpdk_common.h"
#include "rserver_conf.h"

#define API_RS_MODULE_NAME "rserver"
#define API_RS_LIST_NAME "entrys"

struct api_rs {
    int count;
    struct rserver **array;
};

struct api_rs_hdr {
    int cpu_count;
    struct api_rs rs[];
};

static void _api_rs_batch_free(struct api_rs *rs)
{
    if (rs == NULL) {
        return;
    }

    for (int i = 0; i < rs->count; i++) {
        rs_conf_rs_free(rs->array[i]);
    }

    api_free(rs->array);
    rs->array = NULL;
}

static void _api_rs_hdr_free(struct api_rs_hdr *hdr)
{
    if (hdr == NULL) {
        return;
    }

    for (int i = 0; i < hdr->cpu_count; i++) {
        _api_rs_batch_free(&hdr->rs[i]);
    }

    api_free(hdr);
}

static void _api_rs_hdr_only_frame_free(struct api_rs_hdr *hdr)
{
    if (hdr == NULL) {
        return;
    }

    for (int i = 0; i < hdr->cpu_count; i++) {
        api_free(hdr->rs[i].array);
    }

    api_free(hdr);
}

static int _api_rs_add_parse_data(struct api_rs *one, int hw_numa_id, void *json)
{
    int code = 0;
    int port = 0;
    int count = 0;
    void *array = NULL;
    const char *ip = NULL;

    array = api_v1_modify_list(json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (array == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    count = json_array_size(array);
    if (count <= 0) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    one->array = api_malloc_numa(count * sizeof(*one->array), hw_numa_id);
    if (one->array == NULL) {
        return ERRCODE_OOM;
    }

    one->count = count;

    for (int i = 0; i < count; i++) {
        void *obj = NULL;

        obj = json_array_get(array, i);
        ip = json_string_value(json_object_get(obj, "ip"));
        port = json_integer_value(json_object_get(obj, "port"));

        if (strchr(ip, ':') == NULL) { // IPv4
            struct rserver_v4 *v4 = NULL;

            v4 = rs_conf_v4_alloc(hw_numa_id);
            if (v4 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            v4->rs.id = RS_INVALID_ID;
            INIT_LIST_HEAD(&v4->rs.node);
            inet_pton(AF_INET, ip, &v4->ip);
            v4->base.port = port;

            one->array[i] = &v4->rs;
        } else { // IPv6
            struct rserver_v6 *v6 = NULL;

            v6 = rs_conf_v6_alloc(hw_numa_id);
            if (v6 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            v6->rs.id = RS_INVALID_ID;
            INIT_LIST_HEAD(&v6->rs.node);
            inet_pton(AF_INET6, ip, &v6->ip6);
            v6->base.port = port;

            one->array[i] = &v6->rs;
        }
    }

    return 0;

_quit:
    _api_rs_batch_free(one);
    return code;
}

static int _api_rs_del_parse_data(struct api_rs *one, int hw_numa_id, void *json)
{
    int code = 0;
    int port = 0;
    int count = 0;
    void *array = NULL;
    const char *ip = NULL;

    array = api_v1_modify_list(json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (array == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    count = json_array_size(array);
    if (count <= 0) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    one->array = api_malloc_numa(count * sizeof(*one->array), hw_numa_id);
    if (one->array == NULL) {
        return ERRCODE_OOM;
    }

    one->count = count;

    for (int i = 0; i < count; i++) {
        void *obj = NULL;

        obj = json_array_get(array, i);
        ip = json_string_value(json_object_get(obj, "ip"));
        port = json_integer_value(json_object_get(obj, "port"));

        if (strchr(ip, ':') == NULL) { // IPv4
            struct rserver_v4 *v4 = NULL;

            v4 = rs_conf_v4_alloc(hw_numa_id);
            if (v4 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            v4->rs.id = RS_INVALID_ID;
            INIT_LIST_HEAD(&v4->rs.node);
            inet_pton(AF_INET, ip, &v4->ip);
            v4->base.port = port;

            one->array[i] = &v4->rs;
        } else { // IPv6
            struct rserver_v6 *v6 = NULL;

            v6 = rs_conf_v6_alloc(hw_numa_id);
            if (v6 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            v6->rs.id = RS_INVALID_ID;
            INIT_LIST_HEAD(&v6->rs.node);
            inet_pton(AF_INET6, ip, &v6->ip6);
            v6->base.port = port;

            one->array[i] = &v6->rs;
        }
    }

    return 0;

_quit:
    _api_rs_batch_free(one);
    return code;
}

static int _api_rs_copy(struct api_rs *dst, int hw_numa_id, struct api_rs *src)
{
    int code = 0;

    dst->array = api_malloc_numa(src->count * sizeof(*dst->array), hw_numa_id);
    if (dst->array == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    dst->count = src->count;

    for (int i = 0; i < dst->count; i++) {
        struct rserver *rs = NULL;

        rs = src->array[i];
        if (rs->af == AF_INET) {
            struct rserver_v4 *v4 = NULL;

            v4 = rs_conf_v4_alloc(hw_numa_id);
            if (v4 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            dpdk_memcpy(v4, rs, sizeof(*v4));
            dst->array[i] = &v4->rs;
        } else {
            struct rserver_v6 *v6 = NULL;

            v6 = rs_conf_v6_alloc(hw_numa_id);
            if (v6 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            dpdk_memcpy(v6, rs, sizeof(*v6));
            dst->array[i] = &v6->rs;
        }
    }

    return 0;

_quit:
    _api_rs_batch_free(dst);
    return code;
}

static int _api_rs_add_parse(const struct root *root, struct api_rs_hdr **phdr, void *json)
{
    int code = 0;
    struct dataplane *dp = NULL;
    struct api_rs_hdr *hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    hdr = api_malloc(sizeof(struct api_rs_hdr) + cpu_count * sizeof(struct api_rs));
    if (hdr == NULL) {
        return ERRCODE_OOM;
    }

    hdr->cpu_count = cpu_count;

    // one
    dp = root->dpdk_thread[0];
    code = _api_rs_add_parse_data(&hdr->rs[0], dp->hw_numa_id, json);
    if (code != 0) {
        goto _quit;
    }

    // other
    for (int i = 1; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = _api_rs_copy(&hdr->rs[i], dp->hw_numa_id, &hdr->rs[0]);
        if (code != 0) {
            goto _quit;
        }
    }

    *phdr = hdr;
    return 0;

_quit:
    _api_rs_hdr_free(hdr);
    return code;
}

static int _api_rs_del_parse(const struct root *root, struct api_rs_hdr **phdr, void *json)
{
    int code = 0;
    struct dataplane *dp = NULL;
    struct api_rs_hdr *hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    hdr = api_malloc(sizeof(struct api_rs_hdr) + cpu_count * sizeof(struct api_rs));
    if (hdr == NULL) {
        return ERRCODE_OOM;
    }

    hdr->cpu_count = cpu_count;

    // one
    dp = root->dpdk_thread[0];
    code = _api_rs_del_parse_data(&hdr->rs[0], dp->hw_numa_id, json);
    if (code != 0) {
        goto _quit;
    }

    // other
    for (int i = 1; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = _api_rs_copy(&hdr->rs[i], dp->hw_numa_id, &hdr->rs[0]);
        if (code != 0) {
            goto _quit;
        }
    }

    *phdr = hdr;
    return 0;

_quit:
    _api_rs_hdr_free(hdr);
    return code;
}

static void _api_rs_add_del(struct root *root, struct api_rs_hdr *hdr, int cpu_count)
{
    struct api_rs *rs = NULL;
    struct dataplane *dp = NULL;

    for (int i = 0; i < cpu_count; i++) {
        rs = &hdr->rs[i];
        dp = root->dpdk_thread[i];
        rs_conf_add_del(dp->tc->rs_table, rs->array, rs->count);
    }
}

static int _api_rs_add(struct root *root, struct api_rs_hdr *hdr)
{
    int code = 0;
    struct api_rs *rs = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        rs = &hdr->rs[i];
        dp = root->dpdk_thread[i];
        code = rs_conf_add(dp->tc->rs_table, rs->array, rs->count);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_rs_add_del(root, hdr, cpu_count);
    return code;
}

static int _api_rs_del(struct root *root, struct api_rs_hdr *hdr)
{
    int code = 0;
    struct api_rs *rs = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        rs = &hdr->rs[i];
        dp = root->dpdk_thread[i];
        code = rs_conf_del(dp->tc->rs_table, rs->array, rs->count);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    return code;
}

API_POST(/v1/app/rserver, rserver)
{
    int code = 0;
    struct root *root = cfg;
    struct api_rs_hdr *hdr = NULL;

    code = _api_rs_add_parse(root, &hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_rs_add(root, hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_rs_hdr_only_frame_free(hdr);
    return api_succ(NULL);

_quit:
    if (hdr != NULL) {
        _api_rs_hdr_free(hdr);
    }
    return api_fail(code);
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
    struct api_rs_hdr *hdr = NULL;

    code = _api_rs_del_parse(root, &hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_rs_del(root, hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_rs_hdr_free(hdr);
    return api_succ(NULL);

_quit:
    _api_rs_hdr_free(hdr);
    return api_fail(code);
}

API_GET(/v1/app/rserver, rserver)
{
    return api_succ(NULL);
}