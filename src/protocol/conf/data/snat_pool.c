/*****************************************************************************
 * filename: snat_pool.c
 * function:
 * description:
 ****************************************************************************/

#include <stdbool.h>

#include "log.h"
#include "errcode.h"
#include "snat_pool.h"
#include "dpdk_limits.h"
#include "snat_pool_conf.h"

struct snat_table {
    int max_id;
    int count;
    struct snat_pool *store[];
};

static __thread struct snat_table *s_snat_table;

static INLINE void _snat_conf_table_destroy(struct snat_table *table)
{
    if (UNLIKELY(table == NULL)) {
        return;
    }

    dpdk_free(table);
}

static INLINE struct snat_table *_snat_conf_table_create(int count, int hw_numa_id)
{
    size_t size = 0;
    struct snat_table *hdr = NULL;

    size = sizeof(*hdr) + count * sizeof(struct snat_pool *);
    hdr = dpdk_malloc_numa(size, hw_numa_id);
    if (UNLIKELY(hdr == NULL)) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(hdr, 0, size);
    hdr->max_id = count - 1;

    return hdr;
}

static int _snat_conf_table_append_check(const struct snat_table *table, struct snat_pool **pp_snat, int count)
{
    if (UNLIKELY(table->count + count > DP_SNAT_POOL_MAX)) {
        LOG_ERROR("The number of SNAT pools exceeds the threshold.");
        return ERRCODE_SNAT_POOL_EXCEED_THRESHOLD;
    }

    return 0;
}

static int _snat_conf_table_append_create(struct snat_table **dst, const struct snat_table *table, int count, int hw_numa_id)
{
    struct snat_table *target = NULL;
    int cur_count = table->max_id + 1;

    if (table->count + count > cur_count) {
        cur_count = table->count + count;
    }

    target = _snat_conf_table_create(cur_count, hw_numa_id);
    if (UNLIKELY(target == NULL)) {
        return ERRCODE_OOM;
    }

    *dst = target;
    return 0;
}

static int _snat_conf_table_append(struct snat_table *dst, const struct snat_table *src, struct snat_pool *pp_snat[], int count)
{
    int n = 0;
    struct snat_pool *snat = NULL;

    for (int i = 0; i <= src->max_id; i++) {
        dst->store[i] = src->store[i];
    }

    dst->count = src->count;

    for (int i = 0; i <= dst->max_id; i++) {
        if (dst->store[i] != NULL) {
            continue;
        }

        snat = pp_snat[n++];
        snat->id = i;
        dst->store[i] = snat;
        if (n == count) {
            break;
        }
    }

    dst->count += count;
    return 0;
}

void snat_conf_table_destroy(void *hdr)
{
    _snat_conf_table_destroy(hdr);
}

int snat_conf_get_by_id(void *arg, struct snat_pool **pp_snat, uint32_t id)
{
    struct snat_table *table = arg;

    if (UNLIKELY(arg == NULL || pp_snat == NULL || id > table->max_id)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_INNER;
    }

    *pp_snat = table->store[id];
    return 0;
}

int snat_conf_get_by_name(void *arg, struct snat_pool **pp_snat, const char *name)
{
    struct snat_pool *snat = NULL;
    struct snat_table *table = arg;

    if (UNLIKELY(arg == NULL || pp_snat == NULL || name == NULL)) {
        LOG_ERROR("Inner parameter invalid.");
        return ERRCODE_INNER;
    }

    for (int i = 0; i <= table->max_id; i++) {
        snat = table->store[i];
        if (strcmp(snat->name, name) == 0) {
            *pp_snat = snat;
            return 0;
        }
    }

    return ERRCODE_SNAT_POOL_NOT_EXIST;
}

int snat_conf_table_get_count(void *arg, int *p_count)
{
    struct snat_table *table = arg;

    if (UNLIKELY(arg == NULL || p_count == NULL)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *p_count = table->count;
    return 0;
}

int snat_conf_table_get_element(void *arg, struct snat_pool *pp_snat[], int count)
{
    struct snat_table *table = arg;

    if (UNLIKELY(arg == NULL || pp_snat == NULL || count < 0)) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    for (int i = 0; i < table->count; i++) {
        pp_snat[i] = table->store[i];
        if (i == count) {
            break;
        }
    }

    return 0;
}

int snat_conf_table_delete(void **dst, const void *arg, struct snat_pool **pp_snat, int count, int hw_numa_id)
{
    /*int code = 0;
    struct snat_table *target = NULL;
    const struct snat_table *table = (const struct snat_table *) arg;

    if (UNLIKELY(arg == NULL || pp_snat == NULL || count == 0)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _snat_conf_table_delete_check(table, pp_snat, count);
    if (UNLIKELY(code != 0)) {
        return code;
    }

    code = _snat_conf_table_delete_create(&target, table, count, hw_numa_id);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    code = _snat_conf_table_delete(target, table, pp_snat, count);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    *dst = target;
    return 0;

_quit:
    _snat_conf_table_destroy(target);
    return code;*/
    return 0;
}

int snat_conf_table_append(void **dst, const void *arg, struct snat_pool **pp_snat, int count, int hw_numa_id)
{
    int code = 0;
    struct snat_table *target = NULL;
    const struct snat_table *table = (const struct snat_table *) arg;

    if (UNLIKELY(arg == NULL || pp_snat == NULL || count == 0)) {
        LOG_ERROR("Inner invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    code = _snat_conf_table_append_check(table, pp_snat, count);
    if (UNLIKELY(code != 0)) {
        return code;
    }

    code = _snat_conf_table_append_create(&target, table, count, hw_numa_id);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    code = _snat_conf_table_append(target, table, pp_snat, count);
    if (UNLIKELY(code != 0)) {
        goto _quit;
    }

    *dst = target;
    return 0;

_quit:
    _snat_conf_table_destroy(target);
    return code;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

struct snat_pool *snat_get_by_id(uint32_t id)
{
    struct snat_table *table = s_snat_table;
    return table->store[id];
}

void *snat_thread_create(void ***pp_snat_table, int hw_numa_id)
{
    s_snat_table = _snat_conf_table_create(0, hw_numa_id);
    if (UNLIKELY(s_snat_table == NULL)) {
        return NULL;
    }

    *pp_snat_table = (void **)&s_snat_table;

    return s_snat_table;
}

void snat_thread_destroy(void *ptr)
{
    if (ptr != NULL) {
        _snat_conf_table_destroy(ptr);
    }

    if (ptr == s_snat_table) {
        s_snat_table = NULL;
    }
}