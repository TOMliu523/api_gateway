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

#define API_RS_LIST_NAME "entrys"
#define API_RS_MODULE_NAME "rserver"

struct api_rs_base {
    int af;
    uint16_t port;
    union inet_addr addr;
};

struct api_rs {
    int count;
    struct rserver **array;
    struct rserver **delete;
};

struct api_rs_hdr {
    int cpu_count;
    struct api_rs rs[];
};

static void _api_rs_table_destroy(void *rs_thread[], int cpu_count)
{
    for (int i = 0; i < cpu_count; i++) {
        rs_conf_table_destroy(rs_thread[i]);
    }
}

static void _api_rs_batch_free(struct api_rs *rs)
{
    if (rs == NULL) {
        return;
    }

    if (rs->array != NULL) {
        for (int i = 0; i < rs->count; i++) {
            rs_conf_rs_free(rs->array[i]);
        }

        api_free(rs->array);
    }

    if (rs->delete != NULL) {
        api_free(rs->delete);
    }

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

static void _api_rs_v4_init(struct rserver_v4 *v4, uint32_t addr, uint16_t port)
{
    v4->rs.af = AF_INET;
    v4->rs.id = RSERVER_INVALID_ID;
    v4->addr = addr;
    v4->base.port = port;
}

static void _api_rs_v6_init(struct rserver_v6 *v6, struct dpdk_ip6_addr *addr, uint16_t port)
{
    v6->rs.af = AF_INET6;
    v6->rs.id = RSERVER_INVALID_ID;
    dpdk_memcpy(&v6->addr, addr, sizeof(*addr));
    v6->base.port = port;
}

static int _api_rs_add_parse_data(struct api_rs *one, void *json, int hw_numa_id)
{
    int code = 0;
    size_t count = 0;
    uint64_t port = 0;
    void *array = NULL;
    const char *ip = NULL;

    code = api_v1_modify_list(&array, &count, json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (code != 0) {
        return code;
    }

    one->array = api_malloc_numa(count * sizeof(*one->array), hw_numa_id);
    if (one->array == NULL) {
        return ERRCODE_OOM;
    }

    one->count = count;

    for (int i = 0; i < count; i++) {
        void *obj = NULL;

        obj = json_array_get(array, i);
        ip = api_json_get_string(obj, "addr");
        if (ip == NULL) {
            goto _quit;
        }

        code = api_json_get_long(&port, obj, "port");
        if (code != 0) {
            goto _quit;
        }

        if (strchr(ip, ':') == NULL) { // IPv4
            struct rserver_v4 *v4 = NULL;

            v4 = rs_conf_v4_alloc(hw_numa_id);
            if (v4 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            inet_pton(AF_INET, ip, &v4->addr);
            _api_rs_v4_init(v4, v4->addr, (uint16_t)port);

            one->array[i] = &v4->rs;
        } else { // IPv6
            struct rserver_v6 *v6 = NULL;

            v6 = rs_conf_v6_alloc(hw_numa_id);
            if (v6 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            inet_pton(AF_INET6, ip, &v6->addr);
            _api_rs_v6_init(v6, &v6->addr, (uint16_t)port);

            one->array[i] = &v6->rs;
        }
    }

    return 0;

_quit:
    _api_rs_batch_free(one);
    return code;
}

static int _api_rs_del_parse_data(struct api_rs_base *base, void *array, int count)
{
    int code = 0;
    void *obj = NULL;
    uint64_t port = 0;
    const char *addr = NULL;
    struct api_rs_base *one = NULL;

    for (int i = 0; i < count; i++) {
        one = &base[i];
        obj = json_array_get(array, i);

        addr = api_json_get_string(obj, "addr");
        if (addr == NULL) {
            return ERRCODE_PARAMETER_INVALID;
        }

        code = api_json_get_long(&port, obj, "port");
        if (code != 0) {
            return code;
        }

        if (strchr(addr, ':') == NULL) {
            one->af = AF_INET;
        } else {
            one->af = AF_INET6;
        }

        inet_pton(one->af, addr, &one->addr);
        one->port = (port & 0xFFFF);
    }

    return 0;
}

static int _api_rs_del_find(void *table, struct rserver *rss[], struct api_rs_base *base, int count)
{
    int code = 0;
    struct api_rs_base *one = NULL;

    for (int i = 0; i < count; i++) {
        code = rs_conf_find(table, &rss[i], one->af, &one->addr, one->port);
        if (code != 0) {
            LOG_ERROR("Real server not exists.");
            return code;
        }
    }

    return 0;
}

static int _api_rs_del_parse(struct root *root, struct api_rs_hdr **phdr, void *json)
{
    int code = 0;
    size_t count = 0;
    void *array = NULL;
    struct api_rs *rs = NULL;
    struct dataplane *dp = NULL;
    struct api_rs_hdr *hdr = NULL;
    struct api_rs_base *base = NULL;
    int cpu_count = root->hw_info.cpu_count;

    code = api_v1_delete_list(&array, &count, json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (code != 0) {
        return code;;
    }

    base = api_malloc(count * sizeof(*base));
    if (base == NULL) {
        return ERRCODE_OOM;
    }

    code = _api_rs_del_parse_data(base, array, count);
    if (code != 0) {
        goto _quit;
    }

    hdr = api_malloc(sizeof(*hdr) + sizeof(struct api_rs) * cpu_count);
    if (hdr == NULL) {
        goto _quit;
    }

    hdr->cpu_count = cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        rs = &hdr->rs[i];
        rs->delete = api_malloc(sizeof(uint32_t) * count);
        if (rs->delete == NULL) {
            goto _quit;
        }

        rs->count = count;

        dp = root->dpdk_thread[i];
        code = _api_rs_del_find(dp->tc->rs_table, rs->delete, base, count);
        if (code != 0) {
            goto _quit;
        }
    }

    *phdr = hdr;
    api_free(base);

    return 0;

_quit:
    _api_rs_hdr_free(hdr);
    api_free(base);
    return code;
}

static int _api_rs_copy(struct api_rs *dst, struct api_rs *src, int hw_numa_id)
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
        struct rserver_v4 *rs_v4 = NULL;
        struct rserver_v6 *rs_v6 = NULL;
        struct rserver_base *rs_base = NULL;

        rs = src->array[i];
        if (rs->af == AF_INET) {
            rs_v4 = (struct rserver_v4 *)rs;
            rs_base = &rs_v4->base;
        } else {
            rs_v6 = (struct rserver_v6 *)rs;
            rs_base = &rs_v6->base;
        }

        if (rs->af == AF_INET) {
            struct rserver_v4 *v4 = NULL;

            v4 = rs_conf_v4_alloc(hw_numa_id);
            if (v4 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            _api_rs_v4_init(v4, rs_v4->addr, rs_base->port);
            dst->array[i] = &v4->rs;
        } else {
            struct rserver_v6 *v6 = NULL;

            v6 = rs_conf_v6_alloc(hw_numa_id);
            if (v6 == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            _api_rs_v6_init(v6, &rs_v6->addr, rs_base->port);
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
    code = _api_rs_add_parse_data(&hdr->rs[0], json, dp->hw_numa_id);
    if (code != 0) {
        goto _quit;
    }

    // other
    for (int i = 1; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = _api_rs_copy(&hdr->rs[i], &hdr->rs[0], dp->hw_numa_id);
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

static int _api_rs_add(struct root *root, void *rs_thread[], struct api_rs_hdr *hdr)
{
    int code = 0;
    struct api_rs *rs = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        rs = &hdr->rs[i];
        dp = root->dpdk_thread[i];

        code = rs_conf_add(&rs_thread[i], dp->tc->rs_table, (struct rserver **)rs->array, rs->count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_rs_table_destroy(rs_thread, cpu_count);
    return code;
}

static void _api_rs_collection_mutable(void **position[], struct api_rs_hdr *hdr)
{
    int n = 0;
    struct api_rs *ars = NULL;
    struct rserver *rs = NULL;
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;

    for (int i = 0; i < hdr->cpu_count; i++) {
        ars = &hdr->rs[i];
        for (int j = 0; j < ars->count; j++) {
            rs = ars->delete[j];
            if (rs->af == AF_INET) {
                v4 = (struct rserver_v4 *) rs;
                base = &v4->base;
            } else {
                v6 = (struct rserver_v6 *) rs;
                base = &v6->base;
            }

            position[n++] = (void **)&base->mtb;
        }
    }
}

static int _api_rs_del(struct root *root, void *update[], struct api_rs_hdr *hdr)
{
    int n = 0;
    int code = 0;
    struct rserver *one = NULL;
    struct dataplane *dp = NULL;
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_mutable *mtb = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        int count = hdr->rs[i].count;
        struct rserver_base *base = NULL;
        struct rserver **rss = hdr->rs[i].delete;

        dp = root->dpdk_thread[i];

        for (int j = 0; j < count; j++) {
            one = rss[j];
            if (one->af == AF_INET) {
                v4 = (struct rserver_v4 *) one;
                base = &v4->base;
            } else {
                v6 = (struct rserver_v6 *) one;
                base = &v6->base;
            }

            mtb = api_malloc_numa(sizeof(*mtb), dp->hw_numa_id);
            if (mtb == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            mtb->pool_refcnt = base->mtb->pool_refcnt;
            mtb->status = RSERVER_OFFLINE;

            update[n++] = mtb;
        }
    }

    return 0;

_quit:
    for (int i = 0; i < n; i++) {
        api_free(update[i]);
    }
    return code;
}

static int _api_rs_info_get(json_t **ptr, struct rserver *rs)
{
    int ret = 0;
    json_t *obj = NULL;
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;

    obj = json_object();
    if (obj == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    if (rs->af == AF_INET) {
        v4 = (struct rserver_v4 *)rs;
        ret = api_json_add_string(obj, "ip", conf_ip4_to_str(v4->addr));
        if (ret != 0) {
            goto _quit;
        }

        base = &v4->base;
    } else {
        v6 = (struct rserver_v6 *)rs;
        ret = api_json_add_string(obj, "ip", conf_ip6_to_str(&v6->addr));
        if (ret != 0) {
            goto _quit;
        }

        base = &v6->base;
    }

    ret = api_json_add_long(obj, "port", base->port);
    if (ret != 0) {
        goto _quit;
    }

    *ptr = obj;
    return 0;

_quit:
    json_decref(obj);
    return ERRCODE_OOM;
}

API_POST(/v1/app/rserver, rserver)
{
    int code = 0;
    struct root *root = cfg;
    struct dataplane *dp = NULL;
    struct api_rs_hdr *hdr = NULL;
    void **position[CPU_MAX] = {NULL};
    void *rs_thread[CPU_MAX] = {NULL};

    code = _api_rs_add_parse(root, &hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_rs_add(root, rs_thread, hdr);
    if (code != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->rs_table;
    }

    api_thread_config_update(root, position, rs_thread, dpdk_free);
    _api_rs_hdr_only_frame_free(hdr);

    return api_succ(NULL);

_quit:
    _api_rs_hdr_free(hdr);
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
    int count = 0;
    int total_count = 0;
    void **update = NULL;
    void ***position = NULL;
    struct root *root = cfg;
    struct api_rs_hdr *hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    code = _api_rs_del_parse(root, &hdr, json);
    if (code != 0) {
        goto _quit;
    }

    count = hdr->rs[0].count;
    total_count = cpu_count * count;
    update = api_malloc(total_count * sizeof(*update));
    if (update == NULL) {
        goto _quit;
    }

    code = _api_rs_del(root, update, hdr);
    if (code != 0) {
        goto _quit;
    }

    position = api_malloc(total_count * sizeof(*position));
    if (position == NULL) {
        goto _quit;
    }

    _api_rs_collection_mutable(position, hdr);
    api_config_update(position, update, cpu_count * hdr->rs[0].count, dpdk_free);

_quit:
    api_free(update);
    api_free(position);
    _api_rs_hdr_free(hdr);
    if (code == 0) {
        return api_succ(NULL);
    } else {
        return api_fail(code);
    }
}

API_GET(/v1/app/rserver, rserver)
{
    int code = 0;
    int count = 0;
    json_t *obj = NULL;
    json_t *array = NULL;
    struct root *root = cfg;
    struct rserver **rs_array = NULL;
    struct thread_config *tc = root->dpdk_thread[0]->tc;

    array = json_array();
    if (array == NULL) {
        return api_fail(ERRCODE_OOM);
    }

    code = rs_conf_get(tc->rs_table, &count, NULL, 0);
    if (code == 0) {
        return api_succ(array);
    }

    rs_array = api_malloc(sizeof(*rs_array) * count);
    if (rs_array == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    rs_conf_get(tc->rs_table, &count, rs_array, count);
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

    return api_succ(NULL);

_quit:
    json_decref(array);
    return api_fail(code);
}