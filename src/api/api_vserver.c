/*****************************************************************************
 * filename: api_vserver.c
 * function:
 * description:
 ****************************************************************************/

#include <string.h>

#include "l2.h"
#include "list.h"
#include "type.h"
#include "errcode.h"
#include "ip6_conf.h"
#include "ip4_conf.h"
#include "dpdk_port.h"
#include "api_inner.h"
#include "pool_conf.h"
#include "dpdk_core.h"
#include "route4_conf.h"
#include "route6_conf.h"
#include "vserver_conf.h"
#include "snat_pool_conf.h"
#include "api_object_bridge.h"

#define API_VSERVER_MODULE_NAME "vserver"
#define API_VSERVER_LIST_NAME "entries"

struct pool_info {
    int refcnt;
    struct pool *pool;
};

struct snat_pool_info {
    int refcnt;
    struct snat_pool *snat;
};

struct addr_v4_info {
    int refcnt;
    uint16_t port; // interface number
    uint32_t addr;
};

struct addr_v6_info {
    int refcnt;
    uint16_t port; // interface number
    struct dpdk_ip6_addr addr;
};

struct param_info {
    int pool_count;
    struct pool_info pool_info[DP_VSERVER_MAX];

    int snat_count;
    struct snat_pool_info snat_info[DP_VSERVER_MAX];

    int ip4_count;
    struct addr_v4_info v4_info[DP_VSERVER_MAX];

    int ip6_count;
    struct addr_v6_info v6_info[DP_VSERVER_MAX];
};

struct api_param {
    struct {
        int af;
        uint16_t port;
        void *addr_info;
        enum PROTO_TYPE type;
    };
    const char *name;
    const struct vserver *vs;
    const char *pool_name;
    struct pool_info *pool_info;
    const char *snat_pool_name;
    struct snat_pool_info *snat_info;

    struct param_info *info;
};

struct api_param_hdr {
    struct param_info info;

    int count;
    struct api_param param[];
};

struct api_vs {
    int count;
    struct vserver **array[CPU_MAX];

    void *table[CPU_MAX];
};

struct api_pkt {
    int count;
    void *array[DP_VSERVER_MAX];
};

struct api_ip4 {
    int count;
    struct ip4_info array[DP_VSERVER_MAX];

    void *table[CPU_MAX];
};

struct api_ip6 {
    int count;
    struct ip6_info array[DP_VSERVER_MAX];

    void *table[CPU_MAX];
};

struct api_vs_hdr {
    int cpu_count;
    struct api_ip4 ip4;
    struct api_ip6 ip6;
    struct api_vs vs;
    struct api_pkt pkt;
};

static const char *s_type_str[] = {
    "TCP", "UDP", "HTTP", "HTTPS", "HTTP2", "HTTP3",
};

static void _api_vs_ip4_free(struct api_ip4 *ip4, int count)
{
    if (ip4 == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (ip4->table[i] == NULL) {
            continue;
        }

        ip4_conf_table_destroy(ip4->table[i]);
        ip4->table[i] = NULL;
    }
}

static void _api_vs_ip6_free(struct api_ip6 *ip6, int count)
{
    if (ip6 == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (ip6->table[i] == NULL) {
            continue;
        }

        ip6_conf_table_destroy(ip6->table[i]);
        ip6->table[i] = NULL;
    }
}


static void _api_vs_pkt_free(struct api_pkt *pkt)
{
    if (pkt == NULL) {
        return;
    }

    dpdk_pktmbuf_push(pkt->array, pkt->count);
    memset(pkt->array, 0, pkt->count * sizeof(*pkt->array));
    pkt->count = 0;
}

static void _api_vs_free(struct api_vs *p_vs)
{
    struct vserver *one = NULL;

    if (p_vs == NULL) {
        return;
    }

    for (int i = 0; i < CPU_MAX; i++) {
        if (p_vs->array[i] == NULL) {
            continue;
        }

        for (int j = 0; j < p_vs->count; j++) {
            one = p_vs->array[i][j];
            if (one == NULL) {
                api_free(one);
            }

            p_vs->array[i][j] = NULL;
        }

        api_free(p_vs->array[i]);
        p_vs->array[i] = NULL;

        vs_conf_table_destroy(p_vs->table[i]);
        p_vs->table[i] = NULL;
    }
}

static void _api_vs_hdr_free(struct api_vs_hdr *vs_hdr)
{
    if (vs_hdr == NULL) {
        return;
    }

    _api_vs_ip4_free(&vs_hdr->ip4, vs_hdr->cpu_count);
    _api_vs_ip6_free(&vs_hdr->ip6, vs_hdr->cpu_count);
    _api_vs_pkt_free(&vs_hdr->pkt);
    _api_vs_free(&vs_hdr->vs);

    api_free(vs_hdr);
}

static void _api_vs_param_hdr_free(struct api_param_hdr *param_hdr)
{
    if (param_hdr == NULL) {
        return;
    }

    api_free(param_hdr);
}

static void *_api_vs_param_hdr_alloc(int count)
{
    size_t total = 0;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;

    total = sizeof(*param_hdr) + count * sizeof(struct api_param);
    param_hdr = api_malloc(total);
    if (param_hdr == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        param = &param_hdr->param[i];
        param->info = &param_hdr->info;
    }

    param_hdr->count = count;
    return param_hdr;
}

static void *_api_vs_hdr_alloc(void)
{
    struct api_vs_hdr *vs_hdr = NULL;

    vs_hdr = api_malloc(sizeof(*vs_hdr));
    if (vs_hdr == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    return vs_hdr;
}

static int _api_vs_param_type_parse(enum PROTO_TYPE *type, const char *type_str)
{
    for (int i = 0; i < ARR_NUMS(s_type_str); i++) {
        if (strcasecmp(s_type_str[i], type_str) == 0) {
            *type = i;
            return 0;
        }
    }

    LOG_ERROR("Virtual server type not support.");
    return ERRCODE_VSERVER_TYPE_NOT_SUPPORT;
}

static int _api_vs_add_param_parse(struct api_param *param, void *obj)
{
    int code = 0;
    uint64_t port = 0;
    uint8_t port_id = 0;
    uint32_t ip4_addr = 0;
    enum PROTO_TYPE type = 0;
    const char *addr_str = NULL;
    const char *type_str = NULL;
    const char *interface = NULL;
    struct dpdk_ip6_addr ip6_addr = {0};
    struct param_info *info = param->info;

    code = ERRCODE_PARAMETER_INVALID;
    param->name = api_json_get_string(obj, "name");
    if (param->name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    addr_str = api_json_get_string(obj, "addr");
    if (addr_str == NULL) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    interface = api_json_get_string(obj, "interface");
    if (interface == NULL) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    code = api_json_get_long(&port, obj, "port");
    if (code != 0) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    type_str = api_json_get_string(obj, "protocol-type");
    if (type_str == NULL) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    param->pool_name = api_json_get_string(obj, "pool_name");
    if (param->pool_name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    param->snat_pool_name = api_json_get_string(obj, "snat_pool_name");
    if (param->snat_pool_name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return code;
    }

    port_id = dpdk_port_by_name_get(interface);
    if (port_id == UINT8_MAX) {
        LOG_ERROR("Interface name '%s' invalid.", interface);
        return code;
    }

    code = _api_vs_param_type_parse(&type, type_str);
    if (code != 0) {
        return code;
    }

    param->type = type;
    param->port = dpdk_cpu_to_be_16((uint16_t) port);

    if (strchr(addr_str, ':') == NULL) {
        struct addr_v4_info *v4_info = NULL;

        param->af = AF_INET;
        inet_pton(param->af, addr_str, &ip4_addr);
        v4_info = param->addr_info = &info->v4_info[info->ip4_count++];

        v4_info->refcnt += 1;
        v4_info->addr = ip4_addr;
        v4_info->port = port_id;
    } else {
        struct addr_v6_info *v6_info = NULL;

        param->af = AF_INET6;
        inet_pton(param->af, addr_str, &ip6_addr);
        v6_info = param->addr_info = &info->v6_info[info->ip6_count++];

        v6_info->refcnt += 1;
        v6_info->addr = ip6_addr;
        v6_info->port = port_id;
    }

    return 0;
}

static int _api_vs_param_pool(struct api_param *param, void *pool_table)
{
    int code = 0;
    bool has = false;
    struct pool *pool = NULL;
    const char *pool_name = NULL;
    struct pool_info *pool_info = NULL;

    pool_name = param->pool_name;
    for (int i = 0; i < param->info->pool_count; i++) {
        pool_info = &param->info->pool_info[i];

        if (strcmp(pool_name, pool_info->pool->name) == 0) {
            has = true;
            param->pool_info = pool_info;
            break;
        }
    }

    if (!has) {
        pool_info = param->pool_info = &param->info->pool_info[param->info->pool_count++];
    }

    code = pool_conf_get_by_name(&pool, pool_table, pool_name);
    if (code != ERRCODE_POOL_EXISTS) {
        LOG_ERROR("Pool '%s' not exist", pool_name);
        return ERRCODE_POOL_NOT_EXIST;
    }

    pool_info->refcnt += 1;
    pool_info->pool = pool;

    return 0;
}

static int _api_vs_param_snat_pool_parse(struct api_param *param, void *snat_table)
{
    int code = 0;
    bool has = false;
    const char *snat_name = NULL;
    struct snat_pool *snat = NULL;
    struct param_info *param_info = NULL;
    struct snat_pool_info *snat_pool_info = NULL;

    snat_name = param->snat_pool_name;
    param_info = param->info;
    for (int i = 0; i < param_info->snat_count; i++) {
        snat_pool_info = &param_info->snat_info[i];
        if (strcmp(snat_name, snat_pool_info->snat->name) == 0) {
            has = true;
            param->snat_info = snat_pool_info;
            break;
        }
    }

    if (!has) {
        param->snat_info = &param_info->snat_info[param_info->snat_count++];
    }

    code = snat_conf_get_by_name(snat_table, &snat, snat_name);
    if (code != 0) {
        LOG_ERROR("Snat '%s' not exist", snat_name);
        return ERRCODE_SNAT_POOL_NOT_EXIST;
    }

    param->snat_info->refcnt += 1;
    param->snat_info->snat = snat;

    return 0;
}

static int _api_vs_ip4_table_create(struct api_ip4 *ip4, struct root *root, const struct param_info *param_info,
                                    int (*table_create_fn)(void **, void *, const struct ip4_info *, int, int))
{
    int code = 0;
    void *ip4_table = NULL;
    struct ip4_info *info = NULL;
    int cpu_count = root->hw_info.cpu_count;
    const struct addr_v4_info *v4_info = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    // to struct ip4_info
    for (int i = 0; i < param_info->ip4_count; i++) {
        info = &ip4->array[i];
        v4_info = &param_info->v4_info[i];

        code = ip4_info_init(info, v4_info->addr, 32, v4_info->port, info->type, 0);
        if (code != 0) {
            return code;
        }
    }

    ip4->count = param_info->ip4_count;

    // to ip4_table
    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];

        ip4_table = dp->tc->ip4_table;
        code = table_create_fn(&ip4->table[i], ip4_table, ip4->array, ip4->count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_vs_ip4_free(ip4, cpu_count);
    return code;
}

static int _api_vs_ip6_table_create(struct api_ip6 *ip6, struct root *root, const struct param_info *param_info,
                                    int (*table_create_fn)(void **, void *, const struct ip6_info *, int, int))
{
    int code = 0;
    void *ip6_table = NULL;
    void *route6_table = NULL;
    struct ip6_info *info = NULL;
    int cpu_count = root->hw_info.cpu_count;
    const struct addr_v6_info *v6_info = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    route6_table = dp->tc->route6_table;
    // to struct ip6_info
    for (int i = 0; i < param_info->ip6_count; i++) {
        info = &ip6->array[i];
        v6_info = &param_info->v6_info[i];

        dpdk_memcpy(&info->addr, &v6_info->addr, sizeof(v6_info->addr));
        info->port = v6_info->port;
        info->refcnt = v6_info->refcnt;
        info->type = IP_MASTER;

        code = route6_conf_mask_find(&info->mask, route6_table, &info->addr, info->port);
        if (code != 0) {
            return code;
        }
    }

    ip6->count = param_info->ip6_count;
    if (ip6->count == 0) {
        return 0;
    }

    // to ip6_table
    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        ip6_table = dp->tc->ip6_table;
        code = table_create_fn(&ip6->table[i], ip6_table, ip6->array, ip6->count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_vs_ip6_free(ip6, cpu_count);
    return 0;
}

static int _api_vs_pkt_create(struct api_pkt *pkt, const struct root *root, const struct param_info *info)
{
    int ret = 0;
    int code = 0;
    int ip4_count = 0;
    int ip6_count = 0;
    struct dpdk_mac mac = {0};
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;
    const struct addr_v4_info *v4_info = NULL;
    const struct addr_v6_info *v6_info = NULL;

    ip4_count = info->ip4_count;
    ip6_count = info->ip6_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        ret = dpdk_pktmbuf_pop(dp->pktmbuf_pool, pkt->array, ip4_count + ip6_count);
        if (ret == 0) {
            pkt->count = ip4_count + ip6_count;
            break;
        }
    }

    if (ret != 0) {
        LOG_ERROR("Packet mbuf busy.");
        return ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < ip4_count; i++) {
        v4_info = &info->v4_info[i];

        ret = l2_conf_port_mac(v4_info->port, &mac);
        if (ret != 0) {
            code = ERRCODE_INNER;
            goto _quit;
        }

        ret = l2_gratuitous_arp_gen(pkt->array[i], v4_info->port, v4_info->addr, &mac);
        if (ret != 0) {
            code = ERRCODE_INNER;
            goto _quit;
        }
    }

    for (int i = 0; i < ip6_count; i++) {
        v6_info = &info->v6_info[i];

        ret = l2_conf_port_mac(v6_info->port, &mac);
        if (ret != 0) {
            code = ERRCODE_INNER;
            goto _quit;
        }

        ret = ip6_ndp_na_mcast_gen(pkt->array[ip4_count + i], v6_info->port, &v6_info->addr, &mac);
        if (ret != 0) {
            code = ERRCODE_INNER;
            goto _quit;
        }
    }

    return 0;

_quit:
    dpdk_pktmbuf_push(pkt->array, pkt->count);
    return code;
}

static struct vserver *_api_vs_param_to_vserver(const struct api_param *param, int hw_numa_id)
{
    uint32_t pool_id = 0;
    uint32_t snat_pool_id = 0;
    struct vserver_v4 *v4 = NULL;
    struct vserver_v6 *v6 = NULL;
    struct vserver_mutable *mtb = NULL;
    struct vserver_stats *stats = NULL;
    struct addr_v4_info *v4_info = NULL;
    struct addr_v6_info *v6_info = NULL;

    mtb = api_malloc_numa(sizeof(*mtb), hw_numa_id);
    if (mtb == NULL) {
        goto _quit;
    }

    stats = api_malloc_numa(sizeof(*stats), hw_numa_id);
    if (stats == NULL) {
        goto _quit;
    }

    pool_id = param->pool_info->pool->id;
    snat_pool_id = param->snat_info->snat->id;

    if (param->af == AF_INET) {
        v4 = api_malloc_numa(sizeof(*v4), hw_numa_id);
        if (v4 == NULL) {
            goto _quit;
        }

        v4_info = param->addr_info;

        v4->vs.af = AF_INET;
        v4->vs.id = VSERVER_ID_INVALID;
        v4->type = param->type;
        v4->port = param->port;
        v4->vip = v4_info->addr;
        v4->base.pool_id = pool_id;
        v4->base.snat_pool_id = snat_pool_id;
        v4->base.mutable = mtb;
        v4->base.stats = stats;
        dpdk_memcpy(v4->name, param->name, strlen(param->name));

        return &v4->vs;
    } else {
        v6 = api_malloc_numa(sizeof(*v6), hw_numa_id);
        if (v6 == NULL) {
            goto _quit;
        }

        v6_info = param->addr_info;

        v6->vs.af = AF_INET6;
        v6->vs.id = VSERVER_ID_INVALID;
        v6->type = param->type;
        v6->port = param->port;
        dpdk_memcpy(&v6->vip, &v6_info->addr, sizeof(v6->vip));
        v6->base.pool_id = pool_id;
        v6->base.snat_pool_id = snat_pool_id;
        v6->base.mutable = mtb;
        v6->base.stats = stats;
        dpdk_memcpy(v6->name, param->name, strlen(param->name));

        return &v6->vs;
    }

_quit:
    api_free(v4);
    api_free(v6);
    api_free(mtb);
    api_free(stats);
    return NULL;
}

static int _api_vs_table_create(struct api_vs *vs, const struct root *root, const struct api_param_hdr *param_hdr,
                                int (*table_create_fn)(void **, void *, struct vserver **, int, int))
{
    int code = 0;
    int hw_numa_id = 0;
    struct dataplane *dp = NULL;
    int count = param_hdr->count;
    struct vserver *target = NULL;
    struct vserver **pp_vs = NULL;
    const struct api_param *param = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        hw_numa_id = dp->hw_numa_id;
        pp_vs = vs->array[i] = api_malloc_numa(count * sizeof(*pp_vs), hw_numa_id);
        if (pp_vs == NULL) {
            LOG_ERROR("OOM.");
            code = ERRCODE_OOM;
            goto _quit;
        }

        for (int j = 0; j < count; j++) {
            param = &param_hdr->param[j];
            target = _api_vs_param_to_vserver(param, hw_numa_id);
            if (target == NULL) {
                code = ERRCODE_OOM;
                goto _quit;
            }

            pp_vs[j] = target;
        }

        // vserver_table
        code = table_create_fn(&vs->table[i], dp->tc->vs_table, pp_vs, count, hw_numa_id);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    _api_vs_free(vs);
    return code;
}

static int _api_vs_query(void *array, struct vserver *pp_arr[], int count, struct dataplane *dp)
{
    int code = 0;
    void *obj = NULL;
    struct pool *pool = NULL;
    char str[CACHE_LINE] = "";
    struct snat_pool *snat = NULL;
    const struct vserver *vs = NULL;
    const struct vserver_v4 *v4 = NULL;
    const struct vserver_v6 *v6 = NULL;

    for (int i = 0; i < count; i++) {
        code = api_json_object(&obj);
        if (code != 0) {
            goto _quit;
        }

        vs = pp_arr[i];
        if (vs->af == AF_INET) {
            v4 = (const struct vserver_v4 *) vs;

            code = api_json_add_string(obj, "name", v4->name);
            if (code != 0) {
                goto _quit;
            }

            inet_ntop(AF_INET, &v4->vip, str, sizeof(str));
            code = api_json_add_string(obj, "addr", str);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_long(obj, "port", v4->port);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_string(obj, "protocol-type", "TCP");
            if (code != 0) {
                goto _quit;
            }

            code = pool_conf_get_by_id(dp->tc->pool_table, &pool, v4->base.pool_id);
            if (code != 0) {
                goto _quit;
            }

            code = snat_conf_get_by_id(dp->tc->snat_table, &snat, v4->base.snat_pool_id);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_string(obj, "pool_name", pool->name);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_string(obj, "snat_pool_name", snat->name);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_array_append(array, obj);
            if (code != 0) {
                goto _quit;
            }

            obj = NULL;
        } else {
            v6 = (const struct vserver_v6 *) vs;

            code = api_json_add_string(obj, "name", v6->name);
            if (code != 0) {
                goto _quit;
            }

            inet_ntop(AF_INET6, &v6->vip, str, sizeof(str));
            code = api_json_add_string(obj, "addr", str);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_long(obj, "port", v6->port);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_string(obj, "protocol-type", "TCP");
            if (code != 0) {
                goto _quit;
            }

            code = pool_conf_get_by_id(dp->tc->pool_table, &pool, v4->base.pool_id);
            if (code != 0) {
                goto _quit;
            }

            code = snat_conf_get_by_id(dp->tc->snat_table, &snat, v4->base.snat_pool_id);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_string(obj, "pool_name", pool->name);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_add_string(obj, "snat_pool_name", snat->name);
            if (code != 0) {
                goto _quit;
            }

            code = api_json_array_append(array, obj);
            if (code != 0) {
                goto _quit;
            }

            obj = NULL;
        }
    }

    return 0;

_quit:
    api_json_free(obj);
    return code;
}

static int _api_vs_add_param_process(struct api_param *param, struct dataplane *dp)
{
    int code = 0;

    code = _api_vs_param_pool(param, dp->tc->pool_table);
    if (code != 0) {
        return code;
    }

    code = _api_vs_param_snat_pool_parse(param, dp->tc->snat_table);
    if (code != 0) {
        return code;
    }

    return 0;
}

static int _api_vs_add_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    size_t count = 0;
    void *array = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_v1_modify_list(&array, &count, json, API_VSERVER_MODULE_NAME, API_VSERVER_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    param_hdr = _api_vs_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    for (int i = 0; i < count; i++) {
        void *obj = NULL;

        param = &param_hdr->param[i];
        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            LOG_ERROR("Invalid parameter.");
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        code = _api_vs_add_param_parse(param, obj);
        if (code != 0) {
            goto _quit;
        }

        code = _api_vs_add_param_process(param, dp);
        if (code != 0) {
            goto _quit;
        }
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_vs_param_hdr_free(param_hdr);
    return code;
}

static int _api_vs_add(struct api_vs_hdr **pp_vs_hdr, struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_vs_hdr *vs_hdr = NULL;

    vs_hdr = _api_vs_hdr_alloc();
    if (vs_hdr == NULL) {
        return ERRCODE_OOM;
    }

    vs_hdr->cpu_count = root->hw_info.cpu_count;
    code = _api_vs_ip4_table_create(&vs_hdr->ip4, root, &param_hdr->info, ip4_conf_table_create_and_append);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_ip6_table_create(&vs_hdr->ip6, root, &param_hdr->info, ip6_conf_table_create_and_delete);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_pkt_create(&vs_hdr->pkt, root, &param_hdr->info);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_table_create(&vs_hdr->vs, root, param_hdr, vs_conf_table_create_and_append);
    if (code != 0) {
        goto _quit;
    }

    *pp_vs_hdr = vs_hdr;
    return 0;

_quit:
    _api_vs_hdr_free(vs_hdr);
    return code;
}

static int _api_vs_v4_param_info_add(struct param_info *info, struct vserver_v4 *v4)
{
    bool has = false;
    int ip4_count = 0;
    struct addr_v4_info *v4_info = NULL;

    ip4_count = info->ip4_count;
    for (int i = 0; i < ip4_count; i++) {
        v4_info = &info->v4_info[i];

        if (v4->vip == v4_info->addr) {
            has = true;
            v4_info->refcnt -= 1;
            break;
        }
    }

    if (!has) {
        v4_info = &info->v4_info[ip4_count];
        v4_info->addr = v4->vip;
        v4_info->refcnt = -1;
        v4_info->port = v4->port;
    }

    return 0;
}

static int _api_vs_v6_param_info_add(struct param_info *info, struct vserver_v6 *v6)
{
    bool has = false;
    int ip6_count = 0;
    struct addr_v6_info *v6_info = NULL;

    ip6_count = info->ip6_count;
    for (int i = 0; i < ip6_count; i++) {
        v6_info = &info->v6_info[i];

        if (dpdk_ip6_addr_cmp(&v6->vip, &v6_info->addr, sizeof(v6->vip)) == 0) {
            has = true;
            v6_info->refcnt -= 1;
            break;
        }
    }

    if (!has) {
        v6_info = &info->v6_info[ip6_count];
        v6_info->addr = v6->vip;
        v6_info->refcnt = -1;
        v6_info->port = v6->port;
    }

    return 0;
}

static int _api_vs_pool_prepare(struct param_info *param_info, uint32_t id, struct dataplane *dp)
{
    int code = 0;
    bool has = false;
    struct pool *pool = NULL;
    struct pool_info *pool_info = NULL;

    code = pool_conf_get_by_id(dp->tc->pool_table, &pool, id);
    if (code != 0) {
        return code;
    }

    for (int i = 0; i < param_info->pool_count; i++) {
        pool_info = &param_info->pool_info[i];
        if (strcmp(pool->name, pool_info->pool->name) == 0) {
            has = true;
            pool_info->refcnt -= 1;
            return 0;
        }
    }

    if (!has) {
        pool_info->refcnt -= 1;
    }

    return 0;
}

static int _api_vs_snat_pool_prepare(struct param_info *param_info, uint32_t id, struct dataplane *dp)
{
    int code = 0;
    bool has = false;
    struct snat_pool *snat = NULL;
    struct snat_pool_info *snat_info = NULL;

    code = snat_conf_get_by_id(dp->tc->snat_table, &snat, id);
    if (code != 0) {
        return code;
    }

    for (int i = 0; i < param_info->snat_count; i++) {
        snat_info = &param_info->snat_info[i];
        if (strcmp(snat->name, snat_info->snat->name) == 0) {
            has = true;
            snat_info->refcnt -= 1;
            break;
        }
    }

    if (!has) {
        snat_info->refcnt -= 1;
    }

    return 0;
}

static int _api_vs_param_prepare(struct api_param_hdr *param_hdr, struct dataplane *dp)
{
    int code = 0;
    uint32_t pool_id = 0;
    struct vserver *vs = NULL;
    uint32_t snat_pool_id = 0;
    struct vserver_v4 *v4 = NULL;
    struct vserver_v6 *v6 = NULL;
    struct api_param *param = NULL;
    struct param_info *param_info = &param_hdr->info;

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];
        code = vs_conf_get_by_name(dp->tc->vs_table, &vs, param->name);
        if (code != 0) {
            return code;
        }

        if (vs->af == AF_INET) {
            v4 = (struct vserver_v4 *) vs;

            code = _api_vs_v4_param_info_add(param_info, v4);
            if (code != 0) {
                return code;
            }

            pool_id = v4->base.pool_id;
            snat_pool_id = v4->base.snat_pool_id;
        } else {
            v6 = (struct vserver_v6 *) vs;

            code = _api_vs_v6_param_info_add(param_info, v6);
            if (code != 0) {
                return code;
            }

            pool_id = v6->base.pool_id;
            snat_pool_id = v6->base.snat_pool_id;
        }

        code = _api_vs_pool_prepare(param_info, pool_id, dp);
        if (code != 0) {
            return code;
        }

        code = _api_vs_snat_pool_prepare(param_info, snat_pool_id, dp);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_vs_del_parse(struct api_param_hdr **pp_param_hdr, struct root *root, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *param_hdr = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_v1_delete_list(&array, &count, json, API_VSERVER_MODULE_NAME, API_VSERVER_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    param_hdr = _api_vs_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        goto _quit;
    }

    for (int i = 0; i < count; i++) {
        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            goto _quit;
        }

        param = &param_hdr->param[i];
        param->name = api_json_get_string(obj, "name");
        if (param->name == NULL) {
            goto _quit;
        }
    }

    code = _api_vs_param_prepare(param_hdr, dp);
    if (code != 0) {
        goto _quit;
    }

    *pp_param_hdr = param_hdr;
    return 0;

_quit:
    _api_vs_param_hdr_free(param_hdr);
    return code;
}

static int _api_vs_del(struct api_vs_hdr **pp_vs_hdr, struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_vs_hdr *vs_hdr = NULL;

    vs_hdr = api_malloc(sizeof(*vs_hdr));
    if (vs_hdr == NULL) {
        return ERRCODE_OOM;
    }

    vs_hdr->cpu_count = root->hw_info.cpu_count;

    code = _api_vs_ip4_table_create(&vs_hdr->ip4, root, &param_hdr->info, ip4_conf_table_create_and_delete);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_ip6_table_create(&vs_hdr->ip6, root, &param_hdr->info, ip6_conf_table_create_and_delete);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_table_create(&vs_hdr->vs, root, param_hdr, vs_conf_table_create_and_delete);
    if (code != 0) {
        goto _quit;
    }

    return 0;

_quit:
    _api_vs_hdr_free(vs_hdr);
    return code;
}

static void _api_vs_dep_update(struct api_param_hdr *param_hdr)
{
    struct api_param *param = NULL;
    struct pool_info *pool_info = NULL;
    struct snat_pool_info *snat_info = NULL;

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->param[i];

        pool_info = param->pool_info;
        pool_info->pool->refcnt += pool_info->refcnt;

        snat_info = param->snat_info;
        snat_info->snat->refcnt += snat_info->refcnt;
    }
}

/*
 * Only update the dependency index of thread 0.
 * During validation or deletion, check only the index of thread 0.
 * If the object of thread 0 can be safely deleted,
 * all other thread objects are considered deletable as well.
 */
static void _api_vs_update(struct root *root, struct api_vs_hdr *vs_hdr)
{
    struct dataplane *dp = NULL;
    struct api_vs *vs = &vs_hdr->vs;
    void **position[CPU_MAX] = {NULL};
    struct api_ip4 *ip4 = &vs_hdr->ip4;
    struct api_ip6 *ip6 = &vs_hdr->ip6;
    struct api_pkt *pkt = &vs_hdr->pkt;
    int cpu_count = root->hw_info.cpu_count;

    if (ip4->table[0] != NULL) {
        for (int i = 0; i < cpu_count; i++) {
            dp = root->dpdk_thread[i];
            position[i] = &dp->tc->ip4_table;
        }

        api_thread_config_update(root, position, ip4->table, ip4_conf_table_destroy);
    }

    if (ip6->table[0] != NULL) {
        for (int i = 0; i < cpu_count; i++) {
            dp = root->dpdk_thread[i];
            position[i] = &dp->tc->ip6_table;
        }

        api_thread_config_update(root, position, ip6->table, ip6_conf_table_destroy);
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->vs_table;
    }

    api_thread_config_update(root, position, vs->table, vs_conf_table_destroy);

    dp = root->dpdk_thread[0];
    if (pkt->count != 0) {
        dpdk_ring_mp_push(dp->notice_ring, pkt->array, pkt->count);
        memset(pkt->array, 0, pkt->count * sizeof(*pkt->array));
    }
}

API_POST(/v1/network/vserver, vserver)
{
    int code = 0;
    struct root *root = cfg;
    struct api_vs_hdr *vs_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_vs_add_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_add(&vs_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_vs_update(root, vs_hdr);
    _api_vs_dep_update(param_hdr);

_quit:
    _api_vs_hdr_free(vs_hdr);
    _api_vs_param_hdr_free(param_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_PUT(/v1/network/vserver, vserver)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/vserver, vserver)
{
    int code = 0;
    struct root *root = cfg;
    struct api_vs_hdr *vs_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_vs_del_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_del(&vs_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_vs_update(root, vs_hdr);
    _api_vs_dep_update(param_hdr);

_quit:
    _api_vs_hdr_free(vs_hdr);
    _api_vs_param_hdr_free(param_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_GET(/v1/network/vserver, vserver)
{
    int code = 0;
    int count = 0;
    void *array = NULL;
    struct root *root = cfg;
    struct dataplane *dp = NULL;
    struct vserver **pp_vs = NULL;

    code = api_json_array(&array);
    if (code != 0) {
        goto _quit;
    }

    dp = root->dpdk_thread[0];
    code = vs_conf_table_get_count(&count, dp->tc->vs_table);
    if (code != 0) {
        goto _quit;
    }

    if (count == 0) {
        return api_succ(array);
    }

    pp_vs = api_malloc(count * sizeof(*pp_vs));
    if (pp_vs == NULL) {
        LOG_ERROR("OOM.");
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = vs_conf_table_get_element(pp_vs, count, dp->tc->vs_table);
    if (code != 0) {
        goto _quit;
    }

    code = _api_vs_query(array, pp_vs, count, dp);
    if (code != 0) {
        goto _quit;
    }

    api_free(pp_vs);
    return api_succ(array);

_quit:
    api_free(array);
    api_free(pp_vs);
    return api_fail(code);
}