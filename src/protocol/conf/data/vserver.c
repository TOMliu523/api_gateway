/*****************************************************************************
 * filename: vserver.c
 * function:
 * description:
 ****************************************************************************/

#include "log.h"
#include "errcode.h"
#include "dpdk_hash.h"
#include "dpdk_limits.h"
#include "vserver_conf.h"

#define _vs_v4_key_cmp _vs_conf_v4_key_cmp
#define _vs_v6_key_cmp _vs_conf_v6_key_cmp

struct vserver_table {
    struct dpdk_hash *hash4;
    struct dpdk_hash *hash6;

    int count;
    struct vserver *store[];
};

struct v4_key {
    uint32_t addr;
    uint16_t port;
    uint8_t protocol;
};

struct v6_key {
    struct dpdk_ip6_addr addr;
    uint16_t port;
    uint8_t protocol;
};

struct vserver4_kv {
    int count;
    struct v4_key key[DP_MBUF_MAX];
    struct vserver_v4 *vs[DP_MBUF_MAX];
};

struct vserver6_kv {
    int count;
    struct v6_key key[DP_MBUF_MAX];
    struct vserver_v6 *vs[DP_MBUF_MAX];
};

static UNUSED __thread struct vserver4_kv s_vs4_kv;
static UNUSED __thread struct vserver6_kv s_vs6_kv;
static __thread struct vserver_table *s_vs_table;

static INLINE int _vs_conf_v4_key_cmp(const void *arg0, const void *arg1, size_t len)
{
    const struct v4_key *first = (const struct v4_key *) arg0;
    const struct v4_key *second = (const struct v4_key *) arg1;

    if (first->addr == second->addr && first->port == second->port && first->protocol == second->protocol) {
        return 0;
    } else {
        return 1;
    }

    return 0;
}

static INLINE int _vs_conf_v6_key_cmp(const void *arg0, const void *arg1, size_t len)
{
    const struct v6_key *first = (const struct v6_key *) arg0;
    const struct v6_key *second = (const struct v6_key *) arg1;

    if (dpdk_ip6_addr_cmp(&first->addr, &second->addr, sizeof(first->addr)) == 0
        && first->port == second->port && first->protocol == second->protocol) {
        return 0;
    } else {
        return 1;
    }

    return 0;
}

static INLINE void _vs_conf_table_destroy(struct vserver_table *table)
{
    if (UNLIKELY(table == NULL)) {
        return;
    }

    if (table->hash4 != NULL) {
        dpdk_hash_destroy(table->hash4);
        table->hash4 = NULL;
    }

    if (table->hash6 != NULL) {
        dpdk_hash_destroy(table->hash6);
        table->hash6 = NULL;
    }

    dpdk_free(table);
}

static INLINE struct vserver_table *_vs_conf_table_create(int count, int hw_numa_id)
{
    size_t total = 0;
    struct vserver_table *table = NULL;

    total = count * sizeof(struct vserver *) + sizeof(*table);
    table = dpdk_malloc_numa(total, hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        goto _quit;
    }

    memset(table, 0, total);

    table->hash4 = dpdk_hash_create(count, sizeof(struct v4_key), hw_numa_id, _vs_conf_v4_key_cmp);
    if (UNLIKELY(table->hash4 == NULL)) {
        goto _quit;
    }

    table->hash6 = dpdk_hash_create(count, sizeof(struct v6_key), hw_numa_id, _vs_conf_v6_key_cmp);
    if (UNLIKELY(table->hash6 == NULL)) {
        goto _quit;
    }

    table->count = 0;
    return table;

_quit:
    _vs_conf_table_destroy(table);
    return NULL;
}

static int _vs_conf_table_add_check(const struct vserver_table *table, struct vserver **pp_vs, int count)
{
    if (UNLIKELY(table->count + count >= DP_VSERVER_MAX)) {
        LOG_ERROR("The number of virtual servers has exceeded the threshold.");
        return ERRCODE_VSERVER_EXCEED_THRESHOLD;
    }

    return 0;
}

static int _vs_conf_table_del_check(struct vserver_table *table, int count)
{
    if (UNLIKELY(table->count - count < 0)) {
        LOG_ERROR("Inner error.");
        return ERRCODE_INNER;
    }

    return 0;
}

static int _vs_conf_table_add_element(struct vserver_table *table, struct vserver *vs)
{
    int code = 0;
    struct v4_key v4_key = {0};
    struct v6_key v6_key = {0};
    struct vserver_v4 *v4 = NULL;
    struct vserver_v6 *v6 = NULL;

    table->store[table->count++] = vs;
    if (vs->af == AF_INET) {
        v4 = (struct vserver_v4 *) vs;

        v4_key.addr = v4->vip;
        v4_key.port = v4->port;
        v4_key.protocol = VSERVER_TCP;

        code = dpdk_hash_add_kv(table->hash4, &v4_key, vs);
        if (code != 0) {
            LOG_ERROR("Inner invalid parameter.");
            return ERRCODE_INNER;
        }
    } else {
        v6 = (struct vserver_v6 *) vs;

        v6_key.addr = v6->vip;
        v6_key.port = v6->port;
        v6_key.protocol = VSERVER_TCP;

        code = dpdk_hash_add_kv(table->hash6, &v6_key, vs);
        if (code != 0) {
            LOG_ERROR("Inner invalid parameter.");
            return ERRCODE_INNER;
        }
    }

    return 0;
}

static int _vs_conf_table_del(struct vserver_table *dst, struct vserver_table *src, struct vserver *pp_vs[], int count)
{
    int code = 0;
    bool has = false;
    const char *name = NULL;
    struct vserver *vs = NULL;
    struct vserver_v4 *v4 = NULL;
    struct vserver_v6 *v6 = NULL;

    for (int i = 0; i < src->count; i++) {
        has = false;

        vs = src->store[i];
        if (vs->af == AF_INET) {
            v4 = (struct vserver_v4 *) vs;
            name = v4->name;
        } else {
            v6 = (struct vserver_v6 *) vs;
            name = v6->name;
        }

        for (int i = 0; i < count; i++) {
            if (pp_vs[i]->af == AF_INET) {
                struct vserver_v4 *del_v4 = (struct vserver_v4 *)pp_vs[i];
                if (strcmp(name, del_v4->name) == 0) {
                    has = true;
                    break;
                }
            } else {
                struct vserver_v6 *del_v6 = (struct vserver_v6 *)pp_vs[i];
                if (strcmp(name, del_v6->name) == 0) {
                    has = true;
                    break;
                }
            }
        }

        if (!has) {
            code = _vs_conf_table_add_element(dst, vs);
            if (code != 0) {
                return code;
            }
        }
    }

    return 0;
}

static int _vs_conf_table_add(struct vserver_table *dst, const struct vserver_table *src, struct vserver *pp_vs[], int count)
{
    int code = 0;

    for (int i = 0; i < src->count; i++) {
        code = _vs_conf_table_add_element(dst, src->store[i]);
        if (code != 0) {
            return code;
        }
    }

    for (int i = 0; i < count; i++) {
        code = _vs_conf_table_add_element(dst, pp_vs[i]);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

void vs_conf_table_destroy(void *arg)
{
    struct vserver_table *table = arg;

    if (UNLIKELY(arg == NULL)) {
        return;
    }

    _vs_conf_table_destroy(table);
}

int vs_conf_get_by_name(void *arg, struct vserver **pp_vs, const char *name)
{
    struct vserver_v4 *v4 = NULL;
    struct vserver_v6 *v6 = NULL;
    struct vserver *target = NULL;
    struct vserver_table *table = (struct vserver_table *) arg;

    if (UNLIKELY(arg == NULL || name == NULL || *name == 0)) {
        LOG_ERROR("Inner invalid parameter");
        return ERRCODE_INNER;
    }

    for (int i = 0; i < table->count; i++) {
        target = table->store[i];
        if (target->af == AF_INET) {
            v4 = (struct vserver_v4 *) target;
            if (strcmp(v4->name, name) == 0) {
                *pp_vs = &v4->vs;
                return 0;
            }
        } else {
            v6 = (struct vserver_v6 *) target;
            if (strcmp(v6->name, name) == 0) {
                *pp_vs = &v6->vs;
                return 0;
            }
        }
    }

    return ERRCODE_VSERVER_NOT_EXIST;
}

int vs_conf_table_get_element(struct vserver *pp_arr[], int count, void *arg)
{
    struct vserver_table *table = (struct vserver_table *) arg;

    if (UNLIKELY(pp_arr == NULL || arg == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    for (int i = 0; i < table->count; i++) {
        pp_arr[i] = table->store[i];
        if (i == count) {
            break;
        }
    }

    return 0;
}

int vs_conf_table_get_count(int *p_count, const void *arg)
{
    const struct vserver_table *table = (const struct vserver_table *) arg;

    if (UNLIKELY(p_count == NULL || arg == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *p_count = table->count;
    return 0;
}

int vs_conf_table_create_and_delete(void **dst, void *src, struct vserver **pp_vs, int count, int hw_numa_id)
{
    int code = 0;
    struct vserver_table *target = NULL;
    struct vserver_table *table = (struct vserver_table *)src;

    if (UNLIKELY(dst == NULL || src == NULL || pp_vs == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _vs_conf_table_del_check(table, count);
    if (UNLIKELY(code != 0)) {
        return code;
    }

    target = _vs_conf_table_create(table->count - count, hw_numa_id);
    if (UNLIKELY(target == NULL)) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = _vs_conf_table_del(target, table, pp_vs, count);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    return 0;

_quit:
    _vs_conf_table_destroy(target);
    return code;
}

int vs_conf_table_create_and_append(void **dst, void *src, struct vserver **pp_vs, int count, int hw_numa_id)
{
    int code = 0;
    struct vserver_table *target = NULL;
    const struct vserver_table *table = (const struct vserver_table *)src;

    if (UNLIKELY(dst == NULL || src == NULL || pp_vs == NULL)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _vs_conf_table_add_check(table, pp_vs, count);
    if (UNLIKELY(code != 0)) {
        return code;
    }

    target = _vs_conf_table_create(table->count + count, hw_numa_id);
    if (UNLIKELY(target != 0)) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = _vs_conf_table_add(target, table, pp_vs, count);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    return 0;

_quit:
    _vs_conf_table_destroy(target);
    return code;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

void *vserver_thread_create(void ***pp_vs_table, int hw_numa_id)
{
    struct vserver_table *table = NULL;

    table = _vs_conf_table_create(0, hw_numa_id);
    if (UNLIKELY(table == NULL)) {
        return NULL;
    }

    s_vs_table = table;
    *pp_vs_table = (void **)&s_vs_table;

    return table;
}

void vserver_thread_destroy(void *ptr)
{
    _vs_conf_table_destroy(ptr);

    if (ptr == s_vs_table) {
        s_vs_table = NULL;
    }
}

void vserver_thread_config_refresh(void *arg)
{
    s_vs_table = arg;
}