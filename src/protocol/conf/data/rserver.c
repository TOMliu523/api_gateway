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
    int store_count;
    struct rserver *store[];
};

static __thread struct rserver_table *s_rs_table;

static void _rs_conf_free(void *ptr)
{
    if (ptr != NULL) {
        dpdk_free(ptr);
    }
}

static void _rs_conf_table_destroy(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    dpdk_free(ptr);
}

static void *_rs_conf_malloc_numa(size_t size, int hw_numa)
{
    void *ptr = dpdk_malloc_numa(size, hw_numa);
    if (UNLIKELY(ptr == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(ptr, 0, size);
    return ptr;
}

static INLINE bool _rs_conf_del_condition(struct rserver_base *base)
{
    return (base->mtb->status == RSERVER_OFFLINE
            && base->stat->refcnt == 0
            && base->mtb->pool_refcnt);
}

static union inet_addr *_rs_conf_nf_inet_addr_get(struct rserver *rs)
{
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;

    if (rs->af == AF_INET) {
        v4 = (struct rserver_v4 *)rs;
        return (union inet_addr *)&v4->addr;
    } else {
        v6 = (struct rserver_v6 *)rs;
        return (union inet_addr *)&v6->addr;
    }
}

static uint16_t _rs_conf_port_get(struct rserver *rs)
{
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;

    if (rs->af == AF_INET) {
        v4 = (struct rserver_v4 *)rs;
        base = &v4->base;

        return base->port;
    } else {
        v6 = (struct rserver_v6 *)rs;
        base = &v6->base;

        return base->port;
    }
}

static int _rs_conf_has_v4(const struct rserver_table *table, uint32_t addr, uint16_t port)
{
    struct rserver *store = NULL;
    struct rserver_v4 *v4 = NULL;
    struct rserver_base *base = NULL;

    for (int i = 0; i <= table->max_id; i++) {
        store = table->store[i];
        if (store == NULL) {
            continue;
        }

        if (store->af != AF_INET) {
            continue;
        }

        v4 = (struct rserver_v4 *) store;
        if (v4->addr != addr) {
            continue;
        }

        base = &v4->base;
        if (base->port != port) {
            continue;
        }

        return i;
    }

    return -1;
}

static int _rs_conf_has_v6(const struct rserver_table *table, struct dpdk_ip6_addr *addr, uint16_t port)
{
    struct rserver *store = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;

    for (int i = 0; i <= table->max_id; i++) {
        store = table->store[i];
        if (store == NULL) {
            continue;
        }

        if (store->af != AF_INET6) {
            continue;
        }

        v6 = (struct rserver_v6 *) store;
        if (dpdk_ip6_addr_cmp(&v6->addr, addr, sizeof(*addr)) != 0) {
            continue;
        }

        base = &v6->base;
        if (base->port != port) {
            continue;
        }

        return i;
    }

    return -1;
}

static int _rs_conf_has(struct rserver_table *table, struct rserver *one)
{
    struct rserver_v4 *one_v4 = NULL;
    struct rserver_v6 *one_v6 = NULL;
    struct rserver_base *one_base = NULL;

    if (one->af == AF_INET) {
        one_v4 = (struct rserver_v4 *)one;
        one_base = &one_v4->base;

        return _rs_conf_has_v4(table, one_v4->addr, one_base->port);
    } else {
        one_v6 = (struct rserver_v6 *)one;
        one_base = &one_v6->base;

        return _rs_conf_has_v6(table, &one_v6->addr, one_base->port);
    }
}

static int _rs_conf_add_check(void *arg, struct rserver **rs, int count)
{
    int has = 0;
    struct rserver_table *table = arg;

    if (UNLIKELY(table->store_count + count > DP_RSERVER_MAX)) {
        LOG_ERROR("The number(%d) of real servers exceeds the threshold(%d).", table->store_count + count, DP_RSERVER_MAX);
        return ERRCODE_RSERVER_TOO_MANY;
    }

    for (int i = 0; i < count; i++) {
        has = _rs_conf_has(table, rs[i]);
        if (has >= 0) {
            LOG_ERROR("Real server duplicate(ip: %s, port: %d).",
                      conf_ip_to_str(rs[i]->af, _rs_conf_nf_inet_addr_get(rs[i])), _rs_conf_port_get(rs[i]));
            return ERRCODE_RSERVER_DUPLICATE;
        }
    }

    return 0;
}

static int _rs_conf_add(struct rserver_table *dst, struct rserver_table *src, struct rserver *rs[], int count)
{
    int n = 0;

    struct rserver *one = NULL;

    for (int i = 0; i <= dst->max_id; i++) {
        if (one == NULL) {
            if (n != count) {
                one = rs[n++];
            }
        }

        if (src->store[i] != NULL) {
            dst->store[i] = src->store[i];
        } else {
            dst->store[i] = one;
            one = NULL;
        }
    }

    if (UNLIKELY(n != count)) {
        LOG_ERROR("Inner error");
        return ERRCODE_INNER;
    }

    dst->store_count = src->store_count + count;
    return 0;
}

static void _rs_conf_table_copy(struct rserver_table *dst, const struct rserver_table *src)
{
    for (int i = 0; i <= src->max_id; i++) {
        dst->store[i] = src->store[i];
    }

    dst->store_count = src->store_count;
}

static void __rs_conf_table_update(struct rserver_table *table, struct rserver **pp_rs, int count)
{
    struct rserver *rs = NULL;

    for (int i = 0; i < count; i++) {
        rs = pp_rs[i];

        table->store[rs->id] = rs;
    }
}

static void _rs_conf_table_update(struct rserver_table *dst, const struct rserver_table *src, struct rserver **pp_rs, int count)
{
    _rs_conf_table_copy(dst, src);
    __rs_conf_table_update(dst, pp_rs, count);
}

static void _rs_conf_del_offline(struct rserver_table *table)
{
    int id = 0;
    int count = 0;
    int max_id = 0;
    struct rserver *cur = NULL;
    struct rserver_base *base = NULL;

    for (int i = 0 ; i <= table->max_id; i++) {
        if (table->store[i] == NULL) {
            continue;
        }

        cur = table->store[i];

        id = cur->id;
        if (cur->af == AF_INET) {
            base = &((struct rserver_v4 *)cur)->base;
        } else {
            base = &((struct rserver_v6 *)cur)->base;
        }

        if (_rs_conf_del_condition(base)) {
            table->store[id] = NULL;
            rs_conf_rs_free(cur);

            count += 1;
        } else {
            if (id > max_id) {
                max_id = id;
            }
        }
    }

    table->max_id = max_id;
    table->store_count -= count;
}

static void *_rs_conf_table_create(const struct rserver_table *table, int hw_numa_id)
{
    int count = 0;
    uint32_t max = 0;
    size_t total = 0;
    uint32_t max_id = 0;
    struct rserver_table *new_table = NULL;

    max_id = table->max_id;
    max = table->max_id + 1;
    if (max < count + table->store_count) {
        max = count + table->store_count;
        max_id = max - 1;
    }

    total = sizeof(struct rserver_table) + max * sizeof(struct rserver *);
    new_table = dpdk_malloc_numa(total, hw_numa_id);
    if (UNLIKELY(new_table == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(new_table, 0, total);
    new_table->max_id = max_id;

    return new_table;
}

void rs_conf_table_destroy(void *arg)
{
    struct rserver_table *table = arg;

    if (UNLIKELY(table == NULL)) {
        return;
    }

    dpdk_free(table);
}

void rs_conf_batch_refcnt_dec(void *arg, uint32_t ids[], int count)
{
    struct rserver *rs = NULL;
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;
    struct rserver_table *table = arg;

    if (UNLIKELY(arg == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return;
    }

    for (int i = 0; i < count; i++) {
        rs = table->store[ids[i]];
        if (rs->af == AF_INET) {
            v4 = (struct rserver_v4 *)rs;
            base = &v4->base;
        } else {
            v6 = (struct rserver_v6 *)rs;
            base = &v6->base;
        }

        base->mtb->pool_refcnt -= 1;
    }
}

const struct rserver *rs_conf_get_by_id(const void *arg, uint32_t id)
{
    const struct rserver_table *table = arg;

    if (UNLIKELY(table == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return NULL;
    }

    return table->store[id];
}

void rs_conf_refcnt_inc(void *arg, uint32_t id)
{
    struct rserver *rs = NULL;
    struct rserver_v4 *v4 = NULL;
    struct rserver_v6 *v6 = NULL;
    struct rserver_base *base = NULL;
    struct rserver_table *table = arg;

    if (arg == NULL) {
        LOG_ERROR("Invalid parameter.");
        return;
    }

    rs = table->store[id];
    if (rs->af == AF_INET) {
        v4 = (struct rserver_v4 *)rs;
        base = &v4->base;
    } else {
        v6 = (struct rserver_v6 *)rs;
        base = &v6->base;
    }

    base->mtb->pool_refcnt += 1;
}

void rs_conf_rs_free(void *arg)
{
    struct rserver *rserver = arg;
    struct rserver_stat *stat = NULL;
    struct rserver_mutable *mtb = NULL;

    if (rserver == NULL) {
        return;
    }

    if (rserver->af == AF_INET) {
        mtb = ((struct rserver_v4 *)rserver)->base.mtb;
        stat = ((struct rserver_v4 *)rserver)->base.stat;
    } else {
        mtb = ((struct rserver_v4 *)rserver)->base.mtb;
        stat = ((struct rserver_v6 *)rserver)->base.stat;
    }

    _rs_conf_free(mtb);
    _rs_conf_free(stat);
    _rs_conf_free(rserver);
}

void rs_conf_part_free(struct rserver *rs)
{
    if (UNLIKELY(rs == NULL)) {
        return;
    }

    if (rs->af == AF_INET) {
        struct rserver_v4 *v4 = (struct rserver_v4 *) rs;
        dpdk_free(v4->base.mtb);
        dpdk_free(v4);
    } else {
        struct rserver_v6 *v6 = (struct rserver_v6 *) rs;
        dpdk_free(v6->base.mtb);
        dpdk_free(v6);
    }
}

struct rserver_v4 *rs_conf_v4_alloc(int hw_numa)
{
    struct rserver_v4 *v4 = NULL;

    v4 = _rs_conf_malloc_numa(sizeof(*v4), hw_numa);
    if (v4 == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    v4->rs.af = AF_INET;

    v4->base.stat = _rs_conf_malloc_numa(sizeof(*v4->base.stat), hw_numa);
    if (v4->base.stat == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    v4->base.mtb = _rs_conf_malloc_numa(sizeof(*v4->base.mtb), hw_numa);
    if (v4->base.mtb == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    return v4;

_quit:
    rs_conf_rs_free(&v4->rs);
    return NULL;
}

struct rserver_v4 *rs_conf_v4_part_clone(const struct rserver_v4 *v4, int hw_numa_id)
{
    struct rserver_v4 *one = NULL;

    one = _rs_conf_malloc_numa(sizeof(*one), hw_numa_id);
    if (UNLIKELY(one == NULL)) {
        return NULL;
    }

    one->base.mtb = _rs_conf_malloc_numa(sizeof(*one->base.mtb), hw_numa_id);
    if (UNLIKELY(one->base.mtb == NULL)) {
        dpdk_free(one);
        return NULL;
    }

    one->rs.af = AF_INET;
    one->rs.id = v4->rs.id;
    one->addr = v4->addr;
    one->base.stat = v4->base.stat;
    one->base.port = v4->base.port;
    one->base.mtb->status = v4->base.mtb->status;
    one->base.mtb->pool_refcnt = v4->base.mtb->pool_refcnt;

    return one;
}

struct rserver_v6 *rs_conf_v6_part_clone(const struct rserver_v6 *v6, int hw_numa_id)
{
    struct rserver_v6 *one = NULL;

    one = _rs_conf_malloc_numa(sizeof(*one), hw_numa_id);
    if (UNLIKELY(one == NULL)) {
        return NULL;
    }

    one->base.mtb = _rs_conf_malloc_numa(sizeof(*one->base.mtb), hw_numa_id);
    if (UNLIKELY(one->base.mtb == NULL)) {
        dpdk_free(one);
        return NULL;
    }

    one->rs.af = AF_INET6;
    one->rs.id = v6->rs.id;
    dpdk_memcpy(&one->addr, &v6->addr, sizeof(one->addr));
    one->base.stat = v6->base.stat;
    one->base.port = v6->base.port;
    one->base.mtb->status = v6->base.mtb->status;
    one->base.mtb->pool_refcnt = v6->base.mtb->pool_refcnt;

    return one;
}

struct rserver_v6 *rs_conf_v6_alloc(int hw_numa)
{
    struct rserver_v6 *v6 = NULL;

    v6 = _rs_conf_malloc_numa(sizeof(*v6), hw_numa);
    if (v6 == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    v6->rs.af = AF_INET6;

    v6->base.stat = _rs_conf_malloc_numa(sizeof(*v6->base.stat), hw_numa);
    if (v6->base.stat == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    v6->base.mtb = _rs_conf_malloc_numa(sizeof(*v6->base.stat), hw_numa);
    if (v6->base.mtb == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    return v6;

_quit:
    rs_conf_rs_free(&v6->rs);
    return NULL;
}

int rs_conf_get(void *arg, int *out, struct rserver *rs[], int count)
{
    int n = 0;
    struct rserver_table *table = arg;

    if (UNLIKELY(arg == NULL || out == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    if (rs == NULL || count == 0) {
        *out = table->store_count;
        return 0;
    }

    for (int i = 0; i < table->max_id; i++) {
        if (table->store[i] != 0) {
            rs[n++] = table->store[i];
            if (n == count) {
                break;
            }
        }
    }

    *out = n;
    return 0;
}

int rs_conf_find(const void *arg, struct rserver **prs, int af, const union inet_addr *addr, uint16_t port)
{
    int ret = 0;
    const struct rserver_table *table = (const struct rserver_table *)arg;

    if (UNLIKELY(arg == NULL || prs == NULL || addr == NULL)) {
        LOG_ERROR("Inner invalid parameter");
        return ERRCODE_INNER;
    }

    if (af == AF_INET) {
        ret = _rs_conf_has_v4(table, addr->ip, port);
    } else {
        ret = _rs_conf_has_v6(table, (struct dpdk_ip6_addr *)&addr->addr, port);
    }

    if (ret < 0) {
        LOG_ERROR("Real server(%s: %d) not find", conf_ip_to_str(af, addr), port);
        return ERRCODE_RSERVER_NOT_FOUND;
    }

    *prs = table->store[ret];
    return 0;
}

int rs_conf_get_all(const void *arg, struct rserver *rss[], int count)
{
    int n = 0;
    const struct rserver_table *rs_table = arg;

    if (UNLIKELY(arg == NULL || rss == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_INNER;
    }

    for (int i = 0; i <= rs_table->max_id; i++) {
        if (rs_table->store[i] != NULL) {
            rss[n++] = rs_table->store[i];
            if (n == count) {
                break;
            }
        }
    }

    return 0;
}

int rs_conf_table_update(void **dst, void *arg, struct rserver **pp_rs, int count, int hw_numa_id)
{
    struct rserver_table *src_table = arg;
    struct rserver_table *dst_table = NULL;

    if (UNLIKELY(dst == NULL || arg == NULL || pp_rs == NULL || count == 0)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_INNER;
    }

    dst_table = _rs_conf_table_create(src_table, hw_numa_id);
    if (UNLIKELY(dst_table == NULL)) {
        return ERRCODE_OOM;
    }

    _rs_conf_table_update(dst_table, src_table, pp_rs, count);

    *dst = dst_table;
    return 0;
}

int rs_conf_add(void **dst, void *arg, struct rserver **rs, int count, int hw_numa_id)
{
    int code = 0;
    struct rserver_table *table = arg;
    struct rserver_table *new_table = NULL;

    if (UNLIKELY(dst == NULL || arg == NULL || rs == NULL || count == 0)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    _rs_conf_del_offline(table);
    code = _rs_conf_add_check(arg, rs, count);
    if (code != 0) {
        return code;
    }

    new_table = _rs_conf_table_create(table, hw_numa_id);
    if (UNLIKELY(new_table == NULL)) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    code = _rs_conf_add(new_table, table, rs, count);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    *dst = new_table;
    return 0;

_quit:
    _rs_conf_table_destroy(new_table);
    return code;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

void *rserver_thread_create(int hw_numa_id)
{
    struct rserver_table *table = NULL;

    table = dpdk_malloc_numa(sizeof(*table), hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(table, 0, sizeof(*table));

    s_rs_table = table;
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

void rserver_thread_config_refresh(void *arg)
{
    s_rs_table = arg;
}