/*****************************************************************************
 * filename: pool.c
 * function:
 * description:
 ****************************************************************************/

#include "log.h"
#include "list.h"
#include "pool.h"
#include "type.h"
#include "macro.h"
#include "errcode.h"
#include "pool_conf.h"
#include "dpdk_limits.h"
#include "rserver_conf.h"

struct pool_table {
    int max_id;
    int store_count;
    struct pool *store[];
};

static __thread struct pool_table *s_pool_table;

static void _pool_conf_table_destroy(struct pool_table *table)
{
    if (UNLIKELY(table == NULL)) {
        return;
    }

    dpdk_free(table);
}

static struct pool_table *_pool_conf_table_create(int count, int hw_numa_id)
{
    size_t size = 0;
    struct pool_table *table = NULL;

    size = sizeof(*table) + sizeof(struct pool *) * count;
    table = dpdk_malloc_numa(size, hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    table->max_id = count - 1;
    table->store_count = 0;
    memset(table->store, 0, count * sizeof(struct pool *));

    return table;
}

static int _pool_conf_table_clone(struct pool_table **dst, const struct pool_table *src, int hw_numa_id)
{
    struct pool_table *one = NULL;

    one = _pool_conf_table_create(src->max_id + 1, hw_numa_id);
    if (UNLIKELY(one == NULL)) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i <= src->max_id; i++) {
        one->store[one->store_count++] = src->store[i];
    }

    *dst = one;
    return 0;
}

static int _pool_conf_table_clone_extend(struct pool_table **dst, const struct pool_table *src, int count, int hw_numa_id)
{
    int total = 0;
    struct pool_table *one = NULL;

    total = src->store_count + count;
    if (UNLIKELY(total > DP_POOL_MAX)) {
        LOG_ERROR("The number of pool objects exceeds the threshold.");
        return ERRCODE_POOL_TOO_MANY;
    }

    if (total > src->max_id) {
        total = src->max_id + 1;
    }

    one = _pool_conf_table_create(total, hw_numa_id);
    if (UNLIKELY(one == NULL)) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i <= src->max_id; i++) {
        one->store[one->store_count++] = src->store[i];
    }

    *dst = one;
    return 0;
}

static int _pool_conf_table_add(struct pool_table *table, struct pool *pools[], int count)
{
    int n = 0;

    for (int i = 0; i <= table->max_id; i++) {
        if (table->store[i] != NULL) {
            continue;
        }

        table->store[i] = pools[n];
        pools[n]->id = i;
        n += 1;

        if (n == count) {
            return 0;
        }
    }

    LOG_ERROR("Inner unknown error.");
    return ERRCODE_UNKNOWN;
}

static int _pool_conf_table_del(struct pool_table *table, struct pool *pools[], int count)
{
    int i = 0;
    struct pool *one = NULL;

    for (i = 0; i < count; i++) {
        one = pools[i];
        table->store[one->id] = NULL;
    }

    table->store_count -= count;

    for (i = table->max_id; i >= 0; i--) {
        if (table->store[i] != NULL) {
            break;
        }
    }

    table->max_id = i;
    return 0;
}

static uint32_t _pool_rs_rr_get_next(void *arg)
{
    struct pool *pool = arg;
    struct rserver_rr *rr = pool->rr;

    uint32_t idx = rr->next;
    uint32_t id = rr->ids[idx];

    uint32_t n = idx + 1;
    rr->next = (n == rr->rs_count) ? 0 : n;

    return id;
}

void pool_conf_free(struct pool *pool)
{
    if (pool != NULL) {
        dpdk_free(pool->rr);
        dpdk_free(pool);
    }
}

void pool_conf_table_destroy(void *arg)
{
    if (UNLIKELY(arg == NULL)) {
        return;
    }

    _pool_conf_table_destroy(arg);
}

struct pool *pool_conf_create(const char *name, enum RS_SELECT_ALGO type, int rs_count, int hw_numa_id)
{
    size_t size = 0;
    struct pool *pool = NULL;
    size_t name_len = strlen(name);

    size = name_len + 1 + sizeof(struct pool);
    pool = dpdk_malloc_numa(size, hw_numa_id);
    if (UNLIKELY(pool == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(pool, 0, size);

    switch (type) {
    case RS_ALGO_RR:
        pool->pool_rs_get_next = _pool_rs_rr_get_next;

        size = sizeof(struct rserver_rr) + rs_count * sizeof(uint32_t);
        pool->rr = dpdk_malloc_numa(size, hw_numa_id);
        if (UNLIKELY(pool->rr == NULL)) {
            LOG_ERROR("OOM.");
            goto _quit;
        }

        memset(pool->rr, 0, size);
        pool->rr->rs_count = 0;
        pool->rr->next = 0;

        break;
    default:
        LOG_ERROR("Pool select real server algo not support.");
        return NULL;
    }

    pool->id = POOL_ID_INVALID;
    pool->type = type;
    pool->refcnt = 0;
    dpdk_memcpy(pool->name, name, name_len);

    return pool;

_quit:
    pool_conf_free(pool);
    return NULL;
}

int pool_conf_get_by_id(void *arg, struct pool **pp_pool, uint32_t id)
{
    struct pool_table *table = arg;

    if (UNLIKELY(arg != NULL || pp_pool != NULL || id > table->max_id)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *pp_pool = table->store[id];
    return 0;
}

int pool_conf_get_by_name(struct pool **target, void *arg, const char *name)
{
    struct pool *one = NULL;
    struct pool_table *table = (struct pool_table *)arg;

    if (UNLIKELY(arg == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_INNER;
    }

    for (int i = 0; i <= table->max_id; i++) {
        one = table->store[i];
        if (one != NULL && strcmp(one->name, name) == 0) {
            if (target != NULL) {
                *target = one;
            }

            return ERRCODE_POOL_EXISTS;
        }
    }

    return 0;
}

int pool_conf_table_get_count(const void *arg, int *p_count)
{
    const struct pool_table *table = (const struct pool_table *)arg;

    if (UNLIKELY(arg == NULL || p_count == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *p_count = table->store_count;
    return 0;
}

int pool_conf_table_get_element(const struct pool *pools[], const void *arg, int max)
{
    int n = 0;
    const struct pool_table *table = arg;

    if (UNLIKELY(arg == NULL || pools == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    for (int i = 0; i <= table->max_id; i++) {
        if (table->store[i] != NULL) {
            pools[n++] = table->store[i];
            if (n == max) {
                break;
            }
        }
    }

    return 0;
}

int pool_conf_table_del(void **dst, const void *arg, struct pool *pools[], int count, int hw_numa_id)
{
    int code = 0;
    struct pool_table *new_table = NULL;
    const struct pool_table *table = arg;

    if (UNLIKELY(dst == NULL || arg == NULL || pools == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _pool_conf_table_clone(&new_table, table, hw_numa_id);
    if (UNLIKELY(code != 0)) {
        return code;
    }

    code = _pool_conf_table_del(new_table, pools, count);
    if (UNLIKELY(code != 0)) {
        _pool_conf_table_destroy(new_table);
        return code;
    }

    *dst = new_table;
    return 0;
}

int pool_conf_table_add(void **dst, const void *arg, struct pool *pools[], int count, int hw_numa_id)
{
    int code = 0;
    struct pool_table *new_table = NULL;
    const struct pool_table *table = arg;

    if (UNLIKELY(dst == NULL || arg == NULL || pools == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _pool_conf_table_clone_extend(&new_table, table, count, hw_numa_id);
    if (UNLIKELY(code != 0)) {
        return code;
    }

    code = _pool_conf_table_add(new_table, pools, count);
    if (UNLIKELY(code != 0)) {
        _pool_conf_table_destroy(new_table);
        return code;
    }

    *dst = new_table;
    return 0;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

void *pool_thread_create(void ***pp_pool_table, int hw_numa_id)
{
    s_pool_table = _pool_conf_table_create(0, hw_numa_id);

    *pp_pool_table = (void **)&s_pool_table;
    return s_pool_table;
}

void pool_thread_destroy(void *ptr)
{
    if (ptr != s_pool_table) {
        _pool_conf_table_destroy(ptr);
    } else {
        if (s_pool_table != NULL) {
            _pool_conf_table_destroy(s_pool_table);
            s_pool_table = NULL;
        }
    }
}