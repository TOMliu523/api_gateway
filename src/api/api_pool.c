/*****************************************************************************
 * filename: api_pool.c
 * function:
 * description:
 ****************************************************************************/

#include <stdint.h>
#include <arpa/inet.h>

#include "log.h"
#include "type.h"
#include "pool.h"
#include "errcode.h"
#include "pool_conf.h"
#include "api_inner.h"
#include "dpdk_limits.h"
#include "dpdk_common.h"
#include "rserver_conf.h"

#define API_POOL_MODULE_NAME "pool"
#define API_POOL_LIST_NAME "entrys"

struct api_pool_rs {
    int new_refcnt;
    const struct rserver *rs;
};

struct api_pool_rs_hdr {
    int count;
    // dense object table
    struct api_pool_rs *dense[DP_RSERVER_MAX];
    // sparse object list
    struct api_pool_rs sparse[DP_RSERVER_MAX];
};

struct rs_entry {
    int af;
    union inet_addr addr;
    uint16_t port;
    struct api_pool_rs *ptr;
};

struct api_pool_entry {
    char name[CONF_NAME_LEN_MAX];
    const struct pool *pool; // del
    enum RS_SELECT_ALGO type;
    int rs_count;
    struct rs_entry *rs_entry;
};

struct api_pool_hdr {
    struct api_pool_rs_hdr rs_hdr;

    int count;
    struct api_pool_entry *pool_entry;
};

struct api_pool_query_hdr {
    struct rserver *rs[DP_RSERVER_MAX];
    int count;
    struct pool *pools[];
};

static const char *s_pool_rs_select_algo[] = {
    "rr",
    "random",
    "wrr",
};

static void _api_pool_hdr_free(void *ptr)
{
    struct api_pool_hdr *hdr = ptr;

    if (hdr == NULL) {
        return;
    }

    for (int i = 0; i < hdr->count; i++) {
        struct api_pool_entry *entry = &hdr->pool_entry[i];
        if (entry->rs_entry != NULL) {
            dpdk_free(entry->rs_entry);
        }
    }

    dpdk_free(hdr);
}

static void _api_pool_table_batch_free(void *pool_table[], int count)
{
    if (pool_table == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        pool_conf_table_destroy(pool_table[i]);
    }
}

static void _api_pool_rs_table_batch_free(void *rs_table[], int count)
{
    if (rs_table == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        pool_conf_table_destroy(rs_table[i]);
    }
}

static void _api_pool_rs_batch_free(struct rserver *pp_rs[], int count)
{
    if (pp_rs == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (pp_rs[i] == NULL) {
            continue;
        }

        rs_conf_part_free(pp_rs[i]);
        pp_rs[i] = NULL;
    }
}

static void _api_pool_rs_multi_batch_free(struct rserver **ppp_rs[], int batch_count, int count)
{
    if (ppp_rs == NULL) {
        return;
    }

    for (int i = 0; i < batch_count; i++) {
        _api_pool_rs_batch_free(ppp_rs[i], count);
        api_free(ppp_rs[i]);
    }
}

static void _api_pool_batch_free(struct pool *pp_pool[], int count)
{
    if (pp_pool == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        pool_conf_free(pp_pool[i]);
    }
}

static void _api_pool_multi_batch_free(struct pool **ppp_pool[], int batch_count, int count)
{
    if (ppp_pool == NULL) {
        return;
    }

    for (int i = 0; i < batch_count; i++) {
        _api_pool_batch_free(ppp_pool[i], count);
        api_free(ppp_pool[i]);
    }
}

static void _api_pool_rs_array_free(struct rserver **pp_rs[], int count)
{
    for (int i = 0; i < count; i++) {
        dpdk_free(pp_rs[i]);
    }
}

static void _api_pool_array_free(struct pool **pp_pool[], int count)
{
    for (int i = 0; i < count; i++) {
        dpdk_free(pp_pool[i]);
    }
}

static struct api_pool_hdr *_api_pool_hdr_alloc(int count)
{
    size_t size = 0;
    struct api_pool_hdr *hdr = NULL;

    size = sizeof(struct api_pool_hdr) + count * sizeof(struct api_pool_entry);
    hdr = api_malloc(size);
    if (hdr == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    hdr->count = count;
    hdr->pool_entry = (struct api_pool_entry *)(hdr + 1);

    return hdr;
}

static int _api_pool_rs_count(const struct pool *pool)
{
    switch (pool->type) {
    case RS_ALGO_RR:
        return pool->rr->rs_count;
    default:
        return 0;
    }
}

static int _api_pool_post_parse(struct root *root, struct api_pool_hdr **pp, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    const char *ip = NULL;
    struct api_pool_hdr *hdr = NULL;
    struct api_pool_entry *entry = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_v1_modify_list(&array, &count, json, API_POOL_MODULE_NAME, API_POOL_LIST_NAME);
    if (code != 0) {
        return code;
    }

    hdr = _api_pool_hdr_alloc(count);
    if (hdr == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        uint64_t value = 0;
        const char *type = NULL;
        const char *name = NULL;

        obj = json_array_get(array, i);
        entry = &hdr->pool_entry[i];

        name = api_json_get_string(obj, "name");
        if (name == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        code = pool_conf_get_by_name(dp->tc->pool_table, NULL, name);
        if (code != 0) {
            if (code == ERRCODE_POOL_EXISTS) {
                LOG_ERROR("Pool(%s) exists.", name);
            }
            goto _quit;
        }

        strcpy(entry->name, name);

        type = api_json_get_string(obj, "algo");
        if (type == NULL) {
            LOG_ERROR("Invalid parameter.");
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        } else if (strcmp(type, "rr") == 0) {
            entry->type = RS_ALGO_RR;
        } else {
            LOG_ERROR("Pool select algo type not support");
            code = ERRCODE_POOL_NOT_SUPPORT;
            goto _quit;
        }

        code = api_json_get_long(&value, obj, "rs_count");
        if (code != 0) {
            goto _quit;
        }

        entry->rs_count = (int)value;

        entry->rs_entry = api_malloc(entry->rs_count * sizeof(struct rs_entry));
        if (entry->rs_entry == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }

        for (int j = 0; j < entry->rs_count; j++) {
            struct rs_entry *one = &entry->rs_entry[j];

            ip = api_json_get_string(obj, "addr");
            if (ip == NULL) {
                goto _quit;
            }

            code = api_string_to_addr(&one->af, &one->addr, ip);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_get_long(&value, obj, "port");
            if (code != 0) {
                goto _quit;
            }

            one->port = (int)value;
        }
    }

    *pp = hdr;
    return 0;

_quit:
    _api_pool_hdr_free(hdr);
    return code;
}

static int _api_pool_delete_parse(struct root *root, struct api_pool_hdr **phdr, void *json)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    void *array = NULL;
    const char *name = NULL;
    struct pool *pool = NULL;
    struct api_pool_hdr *hdr = NULL;
    struct api_pool_entry *entry = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_v1_delete_list(&array, &count, json, API_POOL_MODULE_NAME, API_POOL_LIST_NAME);
    if (code != 0) {
        return code;
    }

    hdr = _api_pool_hdr_alloc(count);
    if (hdr == NULL) {
        return ERRCODE_OOM;
    }

    for (size_t i = 0; i < count; i++) {
        obj = json_array_get(array, i);
        name = api_json_get_string(obj, "name");
        if (name == NULL) {
            LOG_ERROR("Invalid parameter");
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        code = pool_conf_get_by_name(dp->tc->pool_table, &pool, name);
        if (code != 0) {
            LOG_ERROR("Pool '%s' not exists", name);
            goto _quit;
        }

        if (pool->refcnt != 0) {
            LOG_ERROR("Pool '%s' resource busy.", name);
            goto _quit;
        }

        entry = &hdr->pool_entry[i];
        entry->pool = pool;

        entry->rs_entry = api_malloc(_api_pool_rs_count(pool) * sizeof(struct rs_entry));
        if (entry->rs_entry == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }
    }

    *phdr = hdr;
    return 0;

_quit:
    _api_pool_hdr_free(hdr);
    return code;
}

static int _api_pool_add_prepare(const struct dataplane *dp, struct api_pool_hdr *hdr)
{
    int code = 0;
    uint32_t id = 0;
    struct rserver *rs = NULL;
    struct api_pool_rs *ref = NULL;
    struct rs_entry *rs_entry = NULL;
    struct api_pool_rs_hdr *rs_hdr = NULL;
    struct api_pool_entry *pool_entry = NULL;

    rs_hdr = &hdr->rs_hdr;
    for (int i = 0; i < hdr->count; i++) {
        pool_entry = &hdr->pool_entry[i];
        for (int j = 0; j < pool_entry->rs_count; j++) {
            rs_entry = &pool_entry->rs_entry[j];
            code = rs_conf_find(dp->tc->rs_table, &rs, rs_entry->af, &rs_entry->addr, rs_entry->port);
            if (code != 0) {
                return code;
            }

            id = rs->id;
            ref = &rs_hdr->sparse[id];

            if (ref->rs == NULL) {
                rs_hdr->dense[rs_hdr->count++] = ref;
            }

            ref->rs = rs;
            ref->new_refcnt += 1;

            rs_entry->ptr = ref;
        }
    }

    return 0;
}

static int _api_pool_delete_prepare(const struct dataplane *dp, struct api_pool_hdr *hdr)
{
    uint32_t id = 0;
    struct rserver_rr *rr = NULL;
    const struct rserver *rs = NULL;
    struct api_pool_rs *ref = NULL;
    struct api_pool_rs_hdr *rs_hdr = NULL;

    rs_hdr = &hdr->rs_hdr;
    for (int i = 0; i < hdr->count; i++) {
        const struct pool *pool = NULL;
        struct api_pool_entry *pool_entry = NULL;

        pool = pool_entry->pool;
        pool_entry = &hdr->pool_entry[i];

        switch (pool->type) {
        case RS_ALGO_RR:
            rr = pool->rr;
            for (int j = 0; j < rr->rs_count; j++) {
                rs = rs_conf_get_by_id(dp->tc->rs_table, rr->ids[j]);
                if (rs == NULL) {
                    LOG_ERROR("The real server does not exist in the pool.");
                    return ERRCODE_RSERVER_NOT_FOUND;
                }

                id = rs->id;

                ref = &rs_hdr->sparse[id];
                if (ref->rs == NULL) {
                    rs_hdr->dense[rs_hdr->count++] = ref;
                }

                ref->rs = rs;
                ref->new_refcnt -= 1;

                pool_entry->rs_entry[j].ptr = ref;
            }
        default:
            LOG_ERROR("Pool select algo not support.");
            return ERRCODE_POOL_NOT_SUPPORT;
        }
    }

    return 0;
}

static int _api_pool_rs_part_clone(struct rserver **pp_rs, struct api_pool_rs *pool_rs, int hw_numa_id)
{
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;
    const struct rserver *rs = NULL;

    rs = pool_rs->rs;
    if (rs->af == AF_INET) {
        v4 = rs_conf_v4_part_clone((const struct rserver_v4 *)rs, hw_numa_id);
        if (v4 == NULL) {
            return ERRCODE_OOM;
        }
        base = &v4->base;
    } else {
        v6 = rs_conf_v6_part_clone((const struct rserver_v6 *)rs, hw_numa_id);
        if (v6 == NULL) {
            return ERRCODE_OOM;
        }
        base = &v6->base;
    }

    base->mtb->pool_refcnt += pool_rs->new_refcnt;
    return 0;
}

static int _api_pool_rs_table_make(const struct dataplane *dp, void **rs_table, struct rserver **pp_rs, const struct api_pool_hdr *hdr)
{
    int code = 0;
    const struct api_pool_rs_hdr *rs_hdr = NULL;

    rs_hdr = &hdr->rs_hdr;
    for (int i = 0; i < rs_hdr->count; i++) {
        code = _api_pool_rs_part_clone(&pp_rs[i], rs_hdr->dense[i], dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    code = rs_conf_table_update(rs_table, dp->tc->rs_table, pp_rs, rs_hdr->count, dp->hw_numa_id);
    if (code != 0) {
        goto _quit;
    }

    return 0;

_quit:
    _api_pool_rs_batch_free(pp_rs, rs_hdr->count);
    return code;
}

static int _api_pool_add_table_make(const struct dataplane *dp, void **pool_table, struct pool **pp_pool, const struct api_pool_hdr *hdr)
{
    int code = 0;
    struct pool *one = NULL;
    struct rserver_rr *rr = NULL;
    struct api_pool_entry *entry = NULL;

    for (int i = 0; i < hdr->count; i++) {
        entry = &hdr->pool_entry[i];
        one = pp_pool[i] = pool_conf_create(entry->name, entry->type, entry->rs_count, dp->hw_numa_id);
        if (one == NULL) {
            goto _quit;
        }

        switch (entry->type) {
        case RS_ALGO_RR:
            rr = one->ptr;
            for (int j = 0; j < entry->rs_count; j++) {
                const struct rserver *rs = entry->rs_entry[j].ptr->rs;
                rr->ids[rr->rs_count++] = rs->id;
            }
            break;
        default:
            LOG_ERROR("Pool type not support.");
            code = ERRCODE_POOL_ALGO_NOT_SUPPORT;
            goto _quit;
        }
    }

    code = pool_conf_table_add(pool_table, dp->tc->pool_table, pp_pool, hdr->count, dp->hw_numa_id);
    if (code != 0) {
        goto _quit;
    }

    return 0;

_quit:
    _api_pool_batch_free(pp_pool, hdr->count);
    return code;
}

static int _api_pool_del_table_make(struct dataplane *dp, void **pool_table, struct pool **pp_pool, const struct api_pool_hdr *hdr)
{
    int code = 0;
    struct pool *one = NULL;
    struct api_pool_entry *pool_entry = NULL;

    for (int i = 0; i < hdr->count; i++) {
        pool_entry = &hdr->pool_entry[i];
        code = pool_conf_get_by_name(dp->tc->pool_table, &one, pool_entry->pool->name);
        if (code != 0) {
            return code;
        }

        pp_pool[i] = one;
    }

    code = pool_conf_table_del(pool_table, dp->tc->pool_table, pp_pool, hdr->count, dp->hw_numa_id);
    if (code != 0) {
        return code;
    }

    return 0;
}

static int _api_pool_fill_query_hdr(const struct dataplane *dp, struct api_pool_query_hdr *hdr)
{
    int code = 0;

    code = pool_conf_get_all(dp->tc->pool_table, hdr->pools, hdr->count);
    if (code != 0) {
        return code;
    }

    code = rs_conf_get_all(dp->tc->rs_table, hdr->rs, (int)ARR_NUMS(hdr->rs));
    if (code != 0) {
        return code;
    }

    return 0;
}

static int _api_pool_add(struct root *root, void *pool_table[], void *rs_table[], struct api_pool_hdr *hdr)
{
    int code = 0;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;
    struct pool **pp_pool[CPU_MAX] = {NULL};
    struct rserver **pp_rs[CPU_MAX] = {NULL};

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = _api_pool_add_prepare(dp, hdr);
        if (code != 0) {
            goto _quit;
        }

        pp_pool[i] = api_malloc(hdr->count * sizeof(*pp_pool[i]));
        if (pp_pool[i] == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }

        pp_rs[i] = api_malloc(hdr->rs_hdr.count * sizeof(*pp_rs[i]));
        if (pp_rs[i] == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }

        code = _api_pool_rs_table_make(dp, &rs_table[i], pp_rs[i], hdr);
        if (code != 0) {
            goto _quit;
        }

        code = _api_pool_add_table_make(dp, &pool_table[i], pp_pool[i], hdr);
        if (code != 0) {
            goto _quit;
        }
    }

    _api_pool_rs_array_free(pp_rs, cpu_count);
    _api_pool_array_free(pp_pool, cpu_count);

    return 0;

_quit:
    _api_pool_rs_table_batch_free(rs_table, cpu_count);
    _api_pool_table_batch_free(pool_table, cpu_count);
    _api_pool_rs_multi_batch_free(pp_rs, cpu_count, hdr->rs_hdr.count);
    _api_pool_multi_batch_free(pp_pool, cpu_count, hdr->count);
    return code;
}

static int _api_pool_delete(struct root *root, void *pool_table[], void *rs_table[], struct api_pool_hdr *hdr)
{
    int code = 0;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;
    struct pool **pp_pool[CPU_MAX] = {NULL};
    struct rserver **pp_rs[CPU_MAX] = {NULL};
    struct api_pool_rs_hdr *rs_hdr = &hdr->rs_hdr;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = _api_pool_delete_prepare(dp, hdr);
        if (code != 0) {
            goto _quit;
        }

        pp_pool[i] = api_malloc(hdr->count * sizeof(*pp_pool[i]));
        if (pp_pool[i] == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }

        pp_rs[i] = api_malloc(rs_hdr->count * sizeof(*pp_rs[i]));
        if (pp_rs[i] == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }

        code = _api_pool_rs_table_make(dp, rs_table, pp_rs[i], hdr);
        if (code != 0) {
            goto _quit;
        }

        code = _api_pool_del_table_make(dp, pool_table, pp_pool[i], hdr);
        if (code != 0) {
            goto _quit;
        }
    }

    _api_pool_rs_array_free(pp_rs, cpu_count);
    // pool table and pool free
    _api_pool_multi_batch_free(pp_pool, cpu_count, hdr->count);
    return 0;

_quit:
    _api_pool_rs_multi_batch_free(pp_rs, cpu_count, hdr->rs_hdr.count);
    _api_pool_rs_table_batch_free(rs_table, cpu_count);
    _api_pool_array_free(pp_pool, cpu_count);
    _api_pool_table_batch_free(pool_table, cpu_count);
    return code;
}

static int _api_pool_query(void *array, const struct api_pool_query_hdr *hdr)
{
    int code = 0;
    void *obj = NULL;
    void *subarr = NULL;
    void *subobj = NULL;
    char ip_str[CACHE_LINE] = "";
    const struct pool *pool = NULL;
    const struct rserver *rs = NULL;
    const struct rserver_rr *rr = NULL;
    const struct rserver_v4 *v4 = NULL;
    const struct rserver_v6 *v6 = NULL;
    const struct rserver_base *base = NULL;

    for (int i = 0; i < hdr->count; i++) {
        pool = hdr->pools[i];
        code = api_json_object(&obj);
        if (code != 0) {
            goto _quit;
        }

        code = api_json_add_string(obj, "name", pool->name);
        if (code != 0) {
            goto _quit;
        }

        code = api_json_add_string(obj, "algo", s_pool_rs_select_algo[pool->type]);
        if (code != 0) {
            goto _quit;
        }

        switch (pool->type) {
        case RS_ALGO_RR:
            code = api_json_array(&subarr);
            if (code != 0) {
                goto _quit;
            }

            rr = pool->rr;

            for (int j = 0; j < rr->rs_count; j++) {
                code = api_json_object(&subobj);
                if (code != 0) {
                    goto _quit;
                }

                rs = hdr->rs[rr->ids[j]];
                if (rs->af == AF_INET) {
                    v4 = (struct rserver_v4 *) rs;
                    base = &v4->base;

                    inet_ntop(AF_INET, &v4->addr, ip_str, sizeof(ip_str));
                    code = api_json_add_string(subobj, "addr", ip_str);
                    if (code != 0) {
                        goto _quit;
                    }
                } else {
                    v6 = (struct rserver_v6 *) rs;
                    base = &v6->base;

                    inet_ntop(AF_INET6, &v6->addr, ip_str, sizeof(ip_str));
                    code = api_json_add_string(subobj, "addr", ip_str);
                    if (code != 0) {
                        goto _quit;
                    }
                }

                code = api_json_add_long(subobj, "port", base->port);
                if (code != 0) {
                    goto _quit;
                }

                code = api_json_array_append(subarr, subobj);
                if (code != 0) {
                    goto _quit;
                }

                subobj = NULL;
            }
            break;
        default:
            LOG_ERROR("Pool select real server not support.");
            goto _quit;
        }

        code = api_json_add_object(obj, "rserver", subarr);
        if (code != 0) {
            goto _quit;
        }

        subarr = NULL;

        code = api_json_array_append(array, obj);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    api_json_free(subobj);
    api_json_free(subarr);
    api_json_free(obj);
    return code;
}

static void _api_pool_replace(struct root *root, void **position[], void *rs_table[], void *pool_table)
{
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->rs_table;
    }

    api_thread_config_update(root, position, rs_table, rs_conf_table_destroy);

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->pool_table;
    }

    api_thread_config_update(root, position, pool_table, pool_conf_table_destroy);
}

API_POST(/v1/app/pool, pool)
{
    int code = 0;
    struct root *root = cfg;
    struct api_pool_hdr *hdr = NULL;
    void *rs_table[CPU_MAX] = {NULL};
    void **position[CPU_MAX] = {NULL};
    void *pool_table[CPU_MAX] = {NULL};

    code = _api_pool_post_parse(root, &hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_add(root, pool_table, rs_table, hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_pool_replace(root, position, rs_table, pool_table);

_quit:
    _api_pool_hdr_free(hdr);
    if (code == 0) {
        return api_succ(NULL);
    } else {
        return api_fail(code);
    }
}

API_PUT(/v1/app/pool, pool)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/app/pool, pool)
{
    int code = 0;
    struct root *root = cfg;
    struct api_pool_hdr *hdr = NULL;
    void *rs_table[CPU_MAX] = {NULL};
    void **position[CPU_MAX] = {NULL};
    void *pool_table[CPU_MAX] = {NULL};

    code = _api_pool_delete_parse(root, &hdr, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_delete(root, pool_table, rs_table, hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_pool_replace(root, position, rs_table, pool_table);

_quit:
    _api_pool_hdr_free(hdr);
    if (code != 0) {
        return api_fail(code);
    }

    return api_succ(NULL);
}

API_GET(/v1/app/pool, pool)
{
    int code = 0;
    int count = 0;
    void *array = NULL;
    struct root *root = cfg;
    struct api_pool_query_hdr *hdr = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_json_array(&array);
    if (code != 0) {
        goto _quit;
    }

    code = pool_conf_get_count(dp->tc->pool_table, &count);
    if (code != 0) {
        goto _quit;
    }

    hdr = api_malloc(sizeof(*hdr) + count * sizeof(hdr->pools[0]));
    if (hdr == NULL) {
        goto _quit;
    }

    hdr->count = count;

    code = _api_pool_fill_query_hdr(dp, hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_query(array, hdr);
    if (code != 0) {
        goto _quit;
    }

    api_free(hdr);
    return api_succ(array);

_quit:
    api_free(hdr);
    api_json_free(array);
    return api_fail(code);
}