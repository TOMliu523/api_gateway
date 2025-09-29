/*****************************************************************************
 * filename: rserver.c
 * function:
 * description:
 ****************************************************************************/

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
    int rs_count;
    struct list_head free_list;
    struct list_head v4_search;
    struct list_head v6_search;
    struct rserver *store[DP_RSERVER_MAX];
};

static __thread struct rserver_table *s_rs_table;

static void _rs_conf_free(void *ptr)
{
    if (ptr != NULL) {
        dpdk_free(ptr);
    }
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

static int _rs_conf_v4_check(struct rserver_v4 *rs, struct list_head *head)
{
    struct rserver *cur = NULL;
    struct rserver_v4 *one = NULL;

    list_for_each_entry(cur, head, node) {
        one = (struct rserver_v4 *)cur;

        if (one->ip == rs->ip) {
            if (one->base.port == rs->base.port) {
                LOG_ERROR("Real server duplicate(ip: %s, port: %d)", conf_ip_to_str(rs->ip), rs->base.port);
                return ERRCODE_RSERVER_DUPLICATE;
            } else if (one->base.port < rs->base.port) {
                break;
            } else {
                continue;
            }
        } else if (one->ip < rs->ip) {
            break;
        } else {
            continue;
        }
    }

    return 0;
}

static int _rs_conf_v6_check(struct rserver_v6 *rs, struct list_head *head)
{
    int ret = 0;
    struct rserver *cur = NULL;
    struct rserver_v6 *one = NULL;

    list_for_each_entry(cur, head, node) {
        one = (struct rserver_v6 *)cur;

        ret = dpdk_ip6_addr_cmp(&one->ip6, &rs->ip6, sizeof(one->ip6));
        if (ret == 0) {
            if (one->base.port == rs->base.port) {
                LOG_ERROR("Real server duplicate(ip: %s, port: %d)", conf_ip6_to_str(&rs->ip6), rs->base.port);
                return ERRCODE_RSERVER_DUPLICATE;
            } else if (one->base.port < rs->base.port) {
                break;
            } else {
                continue;
            }
        } else if (ret < 0) {
            break;
        } else {
            continue;
        }
    }

    return 0;
}

static int _rs_conf_add_check(void *arg, struct rserver **rs, int count)
{
    int code = 0;
    struct rserver_table *table = arg;

    if (UNLIKELY(table->rs_count + count > DP_RSERVER_MAX)) {
        LOG_ERROR("The number(%d) of real servers exceeds the threshold(%d).", table->rs_count + count, DP_RSERVER_MAX);
        return ERRCODE_RSERVER_TOO_MANY;
    }

    for (int i = 0; i < count; i++) {
        struct rserver *one = rs[i];

        if (one->af == AF_INET) {
            code = _rs_conf_v4_check((struct rserver_v4 *)one, &table->v4_search);
            if (code != 0) {
                return code;
            }
        } else {
            code = _rs_conf_v6_check((struct rserver_v6 *)one, &table->v6_search);
            if (code != 0) {
                return code;
            }
        }
    }

    return 0;
}

static int _rs_conf_add(void *arg, struct rserver **rs, int count)
{
    int start_id = 0;
    struct rserver *one = NULL;
    struct rserver_table *table = arg;

    for (int i = 0; i < count; i++) {
        one = rs[i];

        while (start_id < DP_RSERVER_MAX) {
            if (table->store[start_id] != NULL) {
                start_id += 1;
            } else {
                table->store[start_id] = one;
                one->id = start_id;
                break;
            }
        }

        /*
         * Data plane threads release backend server indices at different times.
         * Due to deletion operations, the available server count may vary across threads,
         * which could potentially cause errors during addition operations.
         * However, such out-of-bounds scenarios are unlikely to occur in practice.
         */
        if (UNLIKELY(start_id == DP_RSERVER_MAX)) {
            LOG_ERROR("Unexpected boundary condition.");
            return ERRCODE_RSERVER_TOO_MANY;
        }
    }

    return 0;
}

static void _rs_conf_del_offline(void *arg)
{
    struct rserver *cur = NULL;
    struct rserver *next = NULL;
    struct rserver_table *table = arg;
    struct list_head *head = &table->free_list;

    list_for_each_entry_safe(cur, next, head, node) {
        int id = 0;
        struct rserver_v4 *v4 = NULL;
        struct rserver_v6 *v6 = NULL;
        struct rserver_base *base = NULL;

        id = cur->id;
        if (cur->af == AF_INET) {
            base = &v4->base;
        } else {
            base = &v6->base;
        }

        if (base->mtb->status == RSERVER_OFFLINE && base->stat->refcnt == 0 && base->mtb->pool_refcnt == 0) {
            table->store[id] = NULL;
            list_del(&cur->node);
            rs_conf_rs_free(cur);
        }
    }
}

void rs_conf_rs_free(struct rserver *rserver)
{
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

int rs_conf_add(void *arg, struct rserver **rs, int count)
{
    int code = 0;

    _rs_conf_del_offline(arg);

    code = _rs_conf_add_check(arg, rs, count);
    if (code != 0) {
        return code;
    }

   return _rs_conf_add(arg, rs, count);
}

void rs_conf_add_del(void *arg, struct rserver **rs, int count)
{
    struct rserver *one = NULL;
    struct rserver_table *table = arg;

    for (int i = 0; i < count; i++) {
        one = rs[i];

        if (one->id == RS_INVALID_ID) {
            continue;
        }

        table->store[one->id] = NULL;
        list_del(&one->node);
    }
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

void *rserver_init(int hw_numa)
{
    struct rserver_table *table = NULL;

    table = dpdk_malloc_numa(sizeof(*table), hw_numa);
    if ((table == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(table, 0, sizeof(*table));
    INIT_LIST_HEAD(&table->free_list);
    INIT_LIST_HEAD(&table->v4_search);
    INIT_LIST_HEAD(&table->v6_search);

    s_rs_table = table;
    return s_rs_table;
}

void rserver_fini(void *ptr)
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