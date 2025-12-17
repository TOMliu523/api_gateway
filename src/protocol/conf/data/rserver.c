/*****************************************************************************
 * filename: rserver.c
 * function:
 * description:
 ****************************************************************************/

#include <stdio.h>
#include <stdbool.h>

#include "log.h"
#include "conf.h"
#include "list.h"
#include "macro.h"
#include "rserver.h"
#include "errcode.h"
#include "dpdk_ip6.h"
#include "dpdk_common.h"
#include "dpdk_limits.h"
#include "rserver_conf.h"

struct rserver_table {
    int max_id;
    int count;
    struct rserver *store[];
};

static __thread struct rserver_table *s_rs_table;

void _rs_conf_free(void *ptr)
{
    struct rserver *rs = ptr;

    if (UNLIKELY(ptr == NULL)) {
        return;
    }

    if (rs->af == AF_INET) {
        struct rserver4 *v4 = (struct rserver4 *) rs;
        if (v4->base.mtb != NULL) {
            dpdk_free(v4->base.mtb);
            v4->base.mtb = NULL;
        }

        if (v4->base.stat != NULL) {
            dpdk_free(v4->base.stat);
            v4->base.stat = NULL;
        }

        dpdk_free(v4);
    } else {
        struct rserver6 *v6 = (struct rserver6 *) rs;
        if (v6->base.mtb != NULL) {
            dpdk_free(v6->base.mtb);
            v6->base.mtb = NULL;
        }

        if (v6->base.stat != NULL) {
            dpdk_free(v6->base.stat);
            v6->base.stat = NULL;
        }

        dpdk_free(v6);
    }
}

static int _rs_conf_table_append_check(const struct rserver_table *table, int count)
{
    if (UNLIKELY(table->count + count > DP_RSERVER_MAX)) {
        LOG_ERROR("The number of real servers exceeds the threshold.");
        return ERRCODE_RSERVER_TOO_MANY;
    }

    return 0;
}

static int _rs_conf_table_delete_check(const struct rserver_table *table, int count)
{
    if (UNLIKELY(table->count < count)) {
        LOG_ERROR("Real server count error.");
        return ERRCODE_INNER;
    }

    return 0;
}

static void *__rs_conf_table_create(int count, int hw_numa_id)
{
    size_t total = 0;
    struct rserver_table *table = NULL;

    total = sizeof(*table) + count * sizeof(struct rserver *);
    table = dpdk_malloc_numa(total, hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(table, 0, total);
    table->max_id = count - 1;

    return table;
}

static int _rs_conf_table_delete(struct rserver_table *dst, struct rserver_table *src, struct rserver *rs[], int count)
{
    bool has = false;
    struct rserver *one = NULL;

    for (int i = 0; i <= src->max_id; i++) {
        if (src->store[i] == NULL) {
            continue;
        }

        has = false;
        one = src->store[i];

        for (int j = 0; j < count; j++) {
            if (one->id == rs[j]->id) {
                has = true;
                break;
            }
        }

        if (!has) {
            dst->store[i] = one;
        }
    }

    dst->count = src->count - count;
    return 0;
}

static int _rs_conf_table_append(struct rserver_table *dst, struct rserver_table *src, struct rserver *rs[], int count)
{
    int n = 0;

    for (int i = 0; i <= src->max_id; i++) {
        if (src->store[i] != NULL) {
            dst->store[i] = src->store[i];
        } else if (n < count) {
            rs[n]->id = i;
            dst->store[i] = rs[n++];
        }
    }

    if (n >= count) {
        goto _quit;
    }

    for (int i = src->max_id + 1; i <= dst->max_id; i++) {
        if (n < count) {
            rs[n]->id = i;
            dst->store[i] = rs[n++];
        } else {
            break;
        }
    }

_quit:
    dst->count = src->count + count;
    return 0;
}

static int _rs_conf_table_del_and_create(struct rserver_table **pp_dst, struct rserver_table *src, struct rserver *rs[], int count, int hw_numa_id)
{
    int max_id = -1;
    bool has = false;
    struct rserver *one = NULL;
    struct rserver_table *dst = NULL;

    for (int i = 0; i <= src->max_id; i++) {
        if (src->store[i] == NULL) {
            continue;
        }

        has = false;
        one = src->store[i];

        for (int j = 0; j < count; j++) {
            if (one->id == rs[j]->id) {
                has = true;
                break;
            }
        }

        if (!has) {
            max_id = i;
        }
    }

    dst = __rs_conf_table_create(max_id + 1, hw_numa_id);
    if (UNLIKELY(dst == NULL)) {
        return ERRCODE_OOM;
    }

    *pp_dst = dst;
    return 0;
}

void rs_conf_table_destroy(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    dpdk_free(ptr);
}

void rs_conf_free(void *ptr)
{
    return _rs_conf_free(ptr);
}

struct rserver4 *rs_conf_v4_alloc(int hw_numa_id)
{
    struct rserver4 *v4 = NULL;

    v4 = dpdk_malloc_numa(sizeof(*v4), hw_numa_id);
    if (UNLIKELY(v4 == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(v4, 0, sizeof(*v4));

    v4->base.stat = dpdk_malloc_numa(sizeof(*v4->base.stat), hw_numa_id);
    if (v4->base.stat == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(v4->base.stat, 0, sizeof(*v4->base.stat));

    v4->base.mtb = dpdk_malloc_numa(sizeof(*v4->base.mtb), hw_numa_id);
    if (v4->base.mtb == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(v4->base.mtb, 0, sizeof(*v4->base.mtb));
    return v4;

_quit:
    _rs_conf_free(v4);
    return NULL;
}

struct rserver6 *rs_conf_v6_alloc(int hw_numa_id)
{
    struct rserver6 *v6 = NULL;

    v6 = dpdk_malloc_numa(sizeof(*v6), hw_numa_id);
    if (v6 == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(v6, 0, sizeof(*v6));

    v6->base.stat = dpdk_malloc_numa(sizeof(*v6->base.stat), hw_numa_id);
    if (v6->base.stat == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(v6->base.stat, 0, sizeof(*v6->base.stat));

    v6->base.mtb = dpdk_malloc_numa(sizeof(*v6->base.mtb), hw_numa_id);
    if (v6->base.mtb == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(v6->base.mtb, 0, sizeof(*v6->base.mtb));
    return v6;

_quit:
    _rs_conf_free(v6);
    return NULL;
}

int rs_conf_get_by_id(struct rserver **pp_rs, void *arg, uint32_t id)
{
    struct rserver_table *table = (struct rserver_table *)arg;

    if (UNLIKELY(pp_rs == NULL || arg == NULL || id > table->max_id)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *pp_rs = table->store[id];
    return 0;
}

int _rs_conf_table_create(struct rserver_table **pp_dst, const struct rserver_table *src, int count, int hw_numa_id)
{
    int total_count = 0;
    struct rserver_table *table = NULL;

    if (src == NULL) {
        total_count = count;
    } else {
        if (src->count + count > src->max_id + 1) {
            total_count = src->count + count;
        } else {
            total_count = src->max_id + 1;
        }
    }

    table = __rs_conf_table_create(total_count, hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        return ERRCODE_OOM;
    }

    *pp_dst = table;
    return 0;
}

int rs_conf_get_by_key(struct rserver **pp_rs, void *rs_table, int af, const union inet_addr *addr, uint16_t port)
{
    struct rserver *rs = NULL;
    struct rserver4 *v4 = NULL;
    struct rserver6 *v6 = NULL;
    struct rserver_table *table = rs_table;

    if (UNLIKELY(pp_rs == NULL || rs_table == NULL || addr == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    for (int i = 0; i <= table->max_id; i++) {
        rs = table->store[i];
        if (rs == NULL || rs->af != af) {
            continue;
        }

        if (rs->af == AF_INET) {
            v4 = (struct rserver4 *) rs;
            if (v4->addr == addr->ip && v4->base.port == port) {
                *pp_rs = rs;
                return 0;
            }
        } else {
            v6 = (struct rserver6 *) rs;
            if (dpdk_ip6_addr_cmp(&v6->addr, addr, sizeof(*addr)) == 0 && v6->base.port == port) {
                *pp_rs = rs;
                return 0;
            }
        }
    }

    return ERRCODE_RSERVER_NOT_FOUND;
}

int rs_conf_table_get_count(int *p_count, const void *arg)
{
    const struct rserver_table *table = (const struct rserver_table *) arg;

    if (UNLIKELY(p_count == NULL || arg == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *p_count = table->count;
    return 0;
}

int rs_conf_table_get_element(const void *arg, struct rserver *array[], int *p_count)
{
    int n = 0;
    int count = 0;
    const struct rserver_table *table = (const struct rserver_table *) arg;

    if (UNLIKELY(arg == NULL || array == NULL || p_count == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    count = *p_count;
    for (int i = 0; i <= table->max_id; i++) {
        if (table->store[i] != NULL) {
            array[n++] = table->store[i];
            if (n == count) {
                return 0;
            }
        }
    }

    *p_count = n;
    return 0;
}

int rs_conf_table_create_and_delete(void **pp_dst, void *src, struct rserver *rs[], int count, int hw_numa_id)
{
    int code = 0;
    struct rserver_table *dst = NULL;
    struct rserver_table *table = (struct rserver_table *)src;

    if (UNLIKELY(pp_dst == NULL || src == NULL || rs == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _rs_conf_table_delete_check(table, count);
    if (code != 0) {
        return code;
    }

    code = _rs_conf_table_del_and_create(&dst, table, rs, count, hw_numa_id);
    if (code != 0) {
        return code;
    }

    code = _rs_conf_table_delete(dst, src, rs, count);
    if (code != 0) {
        goto _quit;
    }

    *pp_dst = dst;
    return 0;

_quit:
    rs_conf_table_destroy(dst);
    return code;
}

int rs_conf_table_create_and_append(void **pp_dst, void *src, struct rserver *rs[], int count, int hw_numa_id)
{
    int code = 0;
    struct rserver_table *dst = NULL;
    const struct rserver_table *table = src;

    if (UNLIKELY(pp_dst == NULL || src == NULL || rs == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _rs_conf_table_append_check(table, count);
    if (code != 0) {
        return code;
    }

    code = _rs_conf_table_create(&dst, table, count, hw_numa_id);
    if (code != 0) {
        return code;
    }

    code = _rs_conf_table_append(dst, src, rs, count);
    if (code != 0) {
        goto _quit;
    }

    *pp_dst = dst;
    return 0;

_quit:
    rs_conf_table_destroy(dst);
    return code;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

struct rserver *rserver_get_by_id(uint32_t id)
{
    struct rserver_table *table = s_rs_table;
    return table->store[id];
}

void *rserver_thread_create(void ***pp_rs_table, int hw_numa_id)
{
    struct rserver_table *table = NULL;

    table = __rs_conf_table_create(0, hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        return NULL;
    }

    s_rs_table = table;
    *pp_rs_table = (void **)&s_rs_table;

    return s_rs_table;
}

void rserver_thread_destroy(void *ptr)
{
    if (ptr != s_rs_table) {
        struct rserver_table *table = ptr;
        dpdk_free(table);
    } else {
        if (s_rs_table != NULL) {
            dpdk_free(s_rs_table);
            s_rs_table = NULL;
        }
    }
}