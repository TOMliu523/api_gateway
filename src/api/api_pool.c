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
#define API_POOL_LIST_NAME "entries"

struct rserver_list {
    int af;
    uint16_t port;
    union inet_addr addr;
};

struct api_param {
    const char *name;
    enum RS_SELECT_ALGO algo;
    int rs_count;
    struct rserver_list *rs_list;
};

struct api_param_hdr {
    int count;
    struct api_param param[];
};

struct rserver_info {
    struct rserver *rs;
    int refcnt;
};

struct api_pool_hdr {
    int cpu_count;

    int rs_cap;
    int rs_count;
    struct rserver_info *rs_info[CPU_MAX];

    bool need_delete;
    int ele_count;
    struct pool **array[CPU_MAX];

    void *pool_table[CPU_MAX];
};

static const char *s_rs_select_algo[] = {
    "rr",
    "random",
    "wrr",
};

static void _api_pool_hdr_free(struct api_pool_hdr *pool_hdr)
{
    if (pool_hdr == NULL) {
        return;
    }

    for (int i = 0; i < pool_hdr->cpu_count; i++) {
        if (pool_hdr->rs_info[i] != NULL) {
            api_free(pool_hdr->rs_info[i]);
            pool_hdr->rs_info[i] = NULL;
        }

        if (pool_hdr->need_delete) {
            if (pool_hdr->array[i] != NULL) {
                for (int j = 0; j < pool_hdr->ele_count; j++) {
                    if (pool_hdr->array[i][j] != NULL) {
                        pool_conf_free(pool_hdr->array[i][j]);
                        pool_hdr->array[i][j] = NULL;
                    }
                }
            }
        }

        if (pool_hdr->array[i] != NULL) {
            api_free(pool_hdr->array[i]);
            pool_hdr->array[i] = NULL;
        }

        if (pool_hdr->pool_table[i] != NULL) {
            pool_conf_table_destroy(pool_hdr->pool_table[i]);
            pool_hdr->pool_table[i] = NULL;
        }
    }

    api_free(pool_hdr);
}

static void _api_pool_param_hdr_free(struct api_param_hdr *param_hdr)
{
    struct api_param *param = NULL;

    if (param_hdr == NULL) {
        return;
    }

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        if (param->rs_list != NULL) {
            api_free(param->rs_list);
            param->rs_list = NULL;
        }
    }

    api_free(param_hdr);
}

static int _api_pool_hdr_vs_extend(struct api_pool_hdr *pool_hdr, int cap)
{
    struct rserver_info *rs_info = NULL;

    if (cap == 0) {
        return 0;
    }

    for (int i = 0; i < pool_hdr->cpu_count; i++) {
        rs_info = api_malloc(cap * sizeof(*rs_info));
        if (rs_info == NULL) {
            return ERRCODE_OOM;
        }

        pool_hdr->rs_info[i] = rs_info;
    }

    pool_hdr->rs_cap = cap;
    pool_hdr->rs_count = 0;

    return 0;
}

static void *_api_pool_hdr_alloc(int cpu_count, int pool_count, int max_rs_count, bool need_delete)
{
    struct pool **pp_pool = NULL;
    struct rserver_info *rs_info = NULL;
    struct api_pool_hdr *pool_hdr = NULL;

    pool_hdr = api_malloc(sizeof(*pool_hdr));
    if (pool_hdr == NULL) {
        return NULL;
    }

    for (int i = 0; i < cpu_count; i++) {
        if (max_rs_count != 0) {
            rs_info = api_malloc(max_rs_count * sizeof(*rs_info));
            if (rs_info == NULL) {
                goto _quit;
            }

            pool_hdr->rs_info[i] = rs_info;
        }

        pp_pool = api_malloc(pool_count * sizeof(*pp_pool));
        if (pp_pool == NULL) {
            goto _quit;
        }

        pool_hdr->array[i] = pp_pool;
    }

    pool_hdr->cpu_count = cpu_count;
    pool_hdr->rs_cap = max_rs_count;
    pool_hdr->ele_count = pool_count;
    pool_hdr->need_delete = need_delete;

    return pool_hdr;

_quit:
    _api_pool_hdr_free(pool_hdr);
    return NULL;
}

static void *_api_pool_param_hdr_alloc(int count)
{
    int total = 0;
    struct api_param_hdr *param_hdr = NULL;

    total = sizeof(*param_hdr) + count * sizeof(struct api_param);
    param_hdr = api_malloc(total);
    if (param_hdr == NULL) {
        return NULL;
    }

    memset(param_hdr, 0, total);
    param_hdr->count = count;

    return param_hdr;
}

static int _api_pool_rs_cap(struct api_pool_hdr *pool_hdr)
{
    int count = 0;
    struct pool *pool = NULL;

    for (int i = 0; i < pool_hdr->ele_count; i++) {
        pool = pool_hdr->array[0][i];
        count += pool->rr->rs_count;
    }

    return count;
}

static int _api_pool_hdr_extern(struct api_pool_hdr *pool_hdr, struct root *root)
{
    int cap = 0;
    int code = 0;
    int rs_count = 0;
    struct dataplane *dp = NULL;

    cap = _api_pool_rs_cap(pool_hdr);

    dp = root->dpdk_thread[0];
    code = rs_conf_table_get_count(&rs_count, dp->tc->rs_table);
    if (code != 0) {
        return code;
    }

    if (cap > rs_count) {
        cap = rs_count;
    }

    code = _api_pool_hdr_vs_extend(pool_hdr, cap);
    if (code != 0) {
        return code;
    }

    return 0;
}

static int _api_pool_algo_get(enum RS_SELECT_ALGO *p_algo, const char *value)
{
    for (int i = 0; i < ARR_NUMS(s_rs_select_algo); i++) {
        if (strcmp(value, s_rs_select_algo[i]) == 0) {
            *p_algo = (enum RS_SELECT_ALGO) i;
            return 0;
        }
    }

    LOG_ERROR("Invalid parameter.");
    return ERRCODE_PARAMETER_INVALID;
}

static int _api_pool_rs_parse(struct api_param *param, void *obj)
{
    int code = 0;
    size_t count = 0;
    void *array = NULL;
    uint64_t lvalue = 0;
    const char *svalue = NULL;
    struct rserver_list *one = NULL;
    struct rserver_list *rs_list = NULL;

    code = api_json_get_list_info(&array, &count, obj, "rserver", false);
    if (code != 0) {
        return code;
    } else if (code == 0 && count == 0) {
        param->rs_count = 0;
        param->rs_list = NULL;
        return 0;
    }

    rs_list = api_malloc(count * sizeof(struct rserver_list));
    if (rs_list == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (uint64_t i = 0; i < count; i++) {
        void *subobj = NULL;

        one = &rs_list[i];

        subobj = api_json_array_get(array, i);
        if (subobj == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        svalue = api_json_get_string(subobj, "addr");
        if (svalue == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        if (strchr(svalue, ':') == NULL) {
            one->af = AF_INET;
        } else {
            one->af = AF_INET6;
        }

        inet_pton(one->af, svalue, &one->addr);

        code = api_json_get_long(&lvalue, subobj, "port");
        if (code != 0) {
            goto _quit;
        }

        one->port = dpdk_cpu_to_be_16((uint16_t) lvalue);
    }

    param->rs_list = rs_list;
    param->rs_count = (int) count;

    return 0;

_quit:
    api_free(rs_list);
    return code;
}

static int _api_pool_del_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    void *array = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_delete_list(&array, &count, json, API_POOL_MODULE_NAME, API_POOL_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    param_hdr = _api_pool_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (size_t i = 0; i < count; i++) {
        param = &param_hdr->param[i];

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            LOG_ERROR("Invalid parameter.");
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        param->name = api_json_get_string(obj, "name");
        if (param->name == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }
    }

    param_hdr->count = count;
    *pp_param_hdr = param_hdr;

    return 0;

_quit:
    _api_pool_param_hdr_free(param_hdr);
    return code;
}

static int _api_pool_del_get(struct api_pool_hdr *pool_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct pool *pool = NULL;
    struct dataplane *dp = NULL;
    const struct api_param *param = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        for (int j = 0; j < param_hdr->count; j++) {
            param = &param_hdr->param[j];

            code = pool_conf_get_by_name(&pool, dp->tc->pool_table, param->name);
            if (code != ERRCODE_POOL_EXISTS) {
                return code;
            }

            pool_hdr->array[i][j] = pool;
        }
    }

    pool_hdr->ele_count = param_hdr->count;
    return 0;
}

static int _api_pool_del_rs_expand(struct pool *pool, struct dataplane *dp, struct api_pool_hdr *pool_hdr)
{
    int code = 0;
    int count = 0;
    uint32_t rs_id = 0;
    struct rserver *rs = NULL;

    for (int i = 0; i < pool->rr->rs_count; i++) {
        int j = 0;
        bool has = false;
        struct rserver_info *rs_info = NULL;

        rs_id = pool->rr->ids[i];
        code = rs_conf_get_by_id(&rs, dp->tc->rs_table, rs_id);
        if (code != 0) {
            return code;
        }

        rs_info = pool_hdr->rs_info[dp->cpu_id];
        for (j = 0; j < pool_hdr->rs_cap; j++) {
            if (rs_info[j].rs == NULL) {
                count += 1;
                break;
            }

            if (rs_info[j].rs->id == rs_id) {
                has = true;
                rs_info[j].refcnt -= 1;
                break;
            }

            if (!has) {
                rs_info[j].rs = rs;
                rs_info[j].refcnt = -1;
            }
        }
    }

    if (pool_hdr->rs_count == 0) {
        pool_hdr->rs_count = count;
    }
    return 0;
}

static int _api_pool_del_rs_get(struct api_pool_hdr *pool_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct pool *pool = NULL;
    struct dataplane *dp = NULL;
    struct pool **pp_pool = NULL;
    int cpu_count = root->hw_info.cpu_count;

    code = _api_pool_hdr_extern(pool_hdr, root);
    if (code != 0) {
        return code;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        pp_pool = pool_hdr->array[i];

        for (int j = 0; j < pool_hdr->ele_count; j++) {
            pool = pp_pool[j];

            code = _api_pool_del_rs_expand(pool, dp, pool_hdr);
            if (code != 0) {
                return code;
            }
        }
    }

    return 0;
}

static int _api_pool_post_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    void *array = NULL;
    const char *svalue = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = api_v1_modify_list(&array, &count, json, API_POOL_MODULE_NAME, API_POOL_LIST_NAME);
    if (code != 0) {
        return code;
    }

    param_hdr = _api_pool_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        return ERRCODE_OOM;
    }

    for (size_t i = 0; i < count; i++) {
        param = &param_hdr->param[i];
        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        param->name = api_json_get_string(obj, "name");
        if (param->name == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        svalue = api_json_get_string(obj, "algo");
        if (svalue == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        code = _api_pool_algo_get(&param->algo, svalue);
        if (code != 0) {
            goto _quit;
        }

        code = _api_pool_rs_parse(param, obj);
        if (code != 0) {
            goto _quit;
        }
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_pool_param_hdr_free(param_hdr);
    return code;
}

static int _api_pool_rs_process(struct pool *pool, struct dataplane *dp, struct api_pool_hdr *pool_hdr, const struct api_param *param)
{
    int code = 0;
    int count = 0;
    struct rserver *rs = NULL;
    struct rserver_info *info = NULL;
    struct rserver_list *rs_one = NULL;

    if (param->rs_count == 0) {
        return 0;
    }

    for (int i = 0; i < param->rs_count; i++) {
        int j = 0;
        bool has = false;

        rs_one = &param->rs_list[i];
        code = rs_conf_get_by_key(&rs, dp->tc->rs_table, rs_one->af, &rs_one->addr, rs_one->port);
        if (code != 0) {
            return code;
        }

        pool->rr->ids[i] = rs->id;

        info = pool_hdr->rs_info[dp->cpu_id];
        for (j = 0; j < pool_hdr->rs_cap; j++) {
            if (info[j].rs == NULL) {
                count += 1;
                break;
            }

            if (rs->id == info[j].rs->id) {
                has = true;
                info[j].refcnt += 1;
                break;
            }
        }

        if (!has) {
            info[j].rs = rs;
            info[j].refcnt = 1;
        }
    }

    pool->rr->rs_count = param->rs_count;
    if (pool_hdr->ele_count == 0) {
        pool_hdr->ele_count = count;
    }

    return 0;
}

static int _api_pool_rs_to_json(void *array, struct dataplane *dp, struct rserver_rr *rr)
{
    int code = 0;
    void *obj = NULL;
    uint16_t port = 0;
    uint32_t rs_id = 0;
    char str[CACHE_LINE] = "";
    struct rserver *rs = NULL;
    struct rserver4 *v4 = NULL;
    struct rserver6 *v6 = NULL;

    if (rr == NULL || rr->rs_count == 0) {
        return 0;
    }

    for (int i = 0; i < rr->rs_count; i++) {
        rs_id = rr->ids[i];
        code = rs_conf_get_by_id(&rs, dp->tc->rs_table, rs_id);
        if (code != 0) {
            return code;
        }

        code = api_json_object(&obj);
        if (code != 0) {
            return code;
        }

        if (rs->af == AF_INET) {
            v4 = (struct rserver4 *) rs;
            inet_ntop(AF_INET, &v4->addr, str, sizeof(str));

            code = api_json_add_string(obj, "addr", str);
            if (code != 0) {
                api_json_free(obj);
                return code;
            }

            port = dpdk_be_to_cpu_16(v4->base.port);
            code = api_json_add_long(obj, "port", port);
            if (code != 0) {
                api_json_free(obj);
                return code;
            }
        } else {
            v6 = (struct rserver6 *) rs;
            inet_ntop(AF_INET6, &v6->addr, str, sizeof(str));

            code = api_json_add_string(obj, "addr", str);
            if (code != 0) {
                api_json_free(obj);
                return code;
            }

            code = api_json_add_long(obj, "port", v6->base.port);
            if (code != 0) {
                api_json_free(obj);
                return code;
            }
        }

        code = api_json_array_append(array, obj);
        if (code != 0) {
            api_json_free(obj);
            return code;
        }
    }

    return 0;
}

static int _api_pool_to_json(void *obj, struct dataplane *dp, const struct pool *pool)
{
    int code = 0;
    void *array = NULL;

    code = api_json_add_string(obj, "name", pool->name);
    if (code != 0) {
        return code;
    }

    code = api_json_add_string(obj, "algo", s_rs_select_algo[pool->type]);
    if (code != 0) {
        return code;
    }

    code = api_json_array(&array);
    if (code != 0) {
        return code;
    }

    code = _api_pool_rs_to_json(array, dp, pool->rr);
    if (code != 0) {
        api_json_free(array);
        return code;
    }

    code = api_json_add_object(obj, "rserver", array);
    if (code != 0) {
        api_json_free(array);
        return code;
    }

    return 0;
}

static int _api_pool_get(void *array, struct root *root, const struct pool *pp_pool[], int count)
{
    int code = 0;
    void *obj = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    for (int i = 0; i < count; i++) {
        code = api_json_object(&obj);
        if (code != 0) {
            return code;
        }

        code = _api_pool_to_json(obj, dp, pp_pool[i]);
        if (code != 0) {
            api_json_free(obj);
            return code;
        }

        code = api_json_array_append(array, obj);
        if (code != 0) {
            api_json_free(obj);
            return code;
        }
    }

    return 0;
}

static int _pool_conf_table_del(struct api_pool_hdr *pool_hdr, struct root *root)
{
    int code = 0;
    void *pool_table = NULL;
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        code = pool_conf_table_del(&pool_table, dp->tc->pool_table, pool_hdr->array[i], pool_hdr->ele_count, dp->hw_numa_id);
        if (code != 0) {
            return code;
        }

        pool_hdr->pool_table[i] = pool_table;
    }

    return 0;
}

static int _api_pool_del(struct api_pool_hdr **pp_pool_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_pool_hdr *pool_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    pool_hdr = _api_pool_hdr_alloc(cpu_count, param_hdr->count, 0, false);
    if (pool_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = _api_pool_del_get(pool_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_del_rs_get(pool_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _pool_conf_table_del(pool_hdr, root);
    if (code != 0) {
        goto _quit;
    }

    pool_hdr->need_delete = true; // to delete
    *pp_pool_hdr = pool_hdr;
    return 0;

_quit:
    _api_pool_hdr_free(pool_hdr);
    return code;
}

static int _api_pool_add(struct api_pool_hdr **pp_pool_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    int rs_count = 0;
    void *table = NULL;
    void *pool_table = NULL;
    struct pool *pool = NULL;
    struct dataplane *dp = NULL;
    int count = param_hdr->count;
    const struct api_param *param = NULL;
    struct api_pool_hdr *pool_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < count; i++) {
        rs_count += param_hdr->param[i].rs_count;
    }

    pool_hdr = _api_pool_hdr_alloc(cpu_count, count, rs_count <= DP_RSERVER_MAX ? rs_count : DP_RSERVER_MAX , true);
    if (pool_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];

        for (int j = 0; j < count; j++) {
            param = &param_hdr->param[j];

            pool = pool_conf_create(param->name, param->algo, param->rs_count, dp->hw_numa_id);
            if (pool == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            pool_hdr->array[i][j] = pool;

            code = _api_pool_rs_process(pool, dp, pool_hdr, param);
            if (code != 0) {
                goto _quit;
            }
        }

        pool_table = dp->tc->pool_table;
        code = pool_conf_table_append(&table, pool_table, pool_hdr->array[i], pool_hdr->ele_count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }

        pool_hdr->pool_table[i] = table;
    }

    pool_hdr->need_delete = false;
    *pp_pool_hdr = pool_hdr;
    return 0;

_quit:
    _api_pool_hdr_free(pool_hdr);
    return code;
}

static void _api_pool_update(struct api_pool_hdr *pool_hdr, struct root *root)
{
    struct dataplane *dp = NULL;
    void **position[CPU_MAX] = {NULL};
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->pool_table;
    }

    api_thread_config_update(root, position, pool_hdr->pool_table, pool_conf_table_destroy);

    /* Modify the reference count of the real server */
    for (int i = 0; i < cpu_count; i++) {
        struct rserver4 *v4 = NULL;
        struct rserver4 *v6 = NULL;
        struct rserver_info *one = NULL;

        one = pool_hdr->rs_info[i];
        if (one != NULL) {
            for (int j = 0; j < pool_hdr->rs_count; j++) {
                if (one[j].rs == NULL) {
                    break;
                }

                if (one[j].rs->af == AF_INET) {
                    v4 = (struct rserver4 *)one[j].rs;
                    v4->base.mtb->pool_refcnt += one->refcnt;
                } else {
                    v6 = (struct rserver4 *)one[j].rs;
                    v6->base.mtb->pool_refcnt += one->refcnt;
                }
            }
        }
    }
}

API_POST(/v1/app/pool, pool)
{
    int code = 0;
    struct root *root = cfg;
    struct api_pool_hdr *pool_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_pool_post_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_add(&pool_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_pool_update(pool_hdr, root);

_quit:
    _api_pool_hdr_free(pool_hdr);
    _api_pool_param_hdr_free(param_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_PUT(/v1/app/pool, pool)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/app/pool, pool)
{
    int code = 0;
    struct root *root = cfg;
    struct api_pool_hdr *pool_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_pool_del_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_del(&pool_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_pool_update(pool_hdr, root);

_quit:
    _api_pool_hdr_free(pool_hdr);
    _api_pool_param_hdr_free(param_hdr);

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
    struct pool **pp_pool = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_json_array(&array);
    if (code != 0) {
        goto _quit;
    }

    code = pool_conf_table_get_count(dp->tc->pool_table, &count);
    if (code != 0) {
        goto _quit;
    }

    if (count == 0) {
        goto _quit;
    }

    pp_pool = api_malloc(count * sizeof(*pp_pool));
    if (pp_pool == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = pool_conf_table_get_element((const struct pool **)pp_pool, dp->tc->pool_table, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_pool_get(array, root, (const struct pool **)pp_pool, count);
    if (code != 0) {
        goto _quit;
    }

_quit:
    api_free(pp_pool);

    if (code != 0) {
        api_json_free(array);
        return api_fail(code);
    }

    return api_succ(array);
}