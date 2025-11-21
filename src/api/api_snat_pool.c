/*****************************************************************************
 * filename: api_snat_pool.c
 * function:
 * description:
 ****************************************************************************/

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "snat_pool.h"
#include "ip4_conf.h"
#include "ip6_conf.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_limits.h"
#include "route4_conf.h"
#include "route6_conf.h"
#include "snat_pool_conf.h"
#include "api_object_bridge.h"

#define API_SNAT_MODULE_NAME "snat_pool"
#define API_SNAT_LIST_NAME "entries"

#define API_SNAT_ADDR_MAX 100000
#define API_SNAT_ADDR_BASE_COUNT 256

struct addr_info {
    int af;
    uint32_t port;
    int refcnt;
    union inet_addr addr;
};

struct addr_info_entry {
    int count;
    struct addr_info info[API_SNAT_ADDR_MAX];
};

struct addr_info_hdr {
    int cap;
    int count;
    struct addr_info **infos;
    struct addr_info_entry *entry;
};

struct api_param {
    const char *name;
    const struct snat_pool *snat;
    uint8_t port; // interface port
    enum SNAT_ADDR_POLICY policy;
    struct addr_info_hdr v4_hdr;
    struct addr_info_hdr v6_hdr;
    uint16_t min_port;
    uint16_t max_port;
};

struct api_param_hdr {
    struct addr_info_entry entry;

    int count;
    struct api_param params[];
};

struct api_snat_hdr {
    int cpu_count;
    bool need_delete;

    int ip4_info_count;
    struct ip4_info *ip4_info[CPU_MAX];
    void *ip4_table[CPU_MAX];

    int ip6_info_count;
    struct ip6_info *ip6_info[CPU_MAX];
    void *ip6_table[CPU_MAX];

    int snat_count;
    struct snat_pool **snat[CPU_MAX];
    void *snat_table[CPU_MAX];
};

static const char *s_snat_addr_policy_name[] = {
    "ip-rr",
    "conn-rr",
    "least-conn",
    "preserve-sport",
};

static void _api_snat_param_free(struct api_param *param)
{
    if (param == NULL) {
        return;
    }

    api_free(param->v4_hdr.infos);
    api_free(param->v6_hdr.infos);

    param->v4_hdr.cap = 0;
    param->v4_hdr.count = 0;

    param->v6_hdr.cap = 0;
    param->v6_hdr.count = 0;
}

static void _api_snat_param_hdr_free(struct api_param_hdr *param_hdr)
{
    struct api_param *param = NULL;
    struct addr_info_hdr *info_hdr = NULL;

    if (param_hdr == NULL) {
        return;
    }

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->params[i];

        info_hdr = &param->v4_hdr;
        if (info_hdr->infos != NULL) {
            api_free(info_hdr->infos);
            info_hdr->infos = NULL;
        }

        info_hdr = &param->v6_hdr;
        if (info_hdr->infos != NULL) {
            api_free(info_hdr->infos);
            info_hdr->infos = NULL;
        }
    }

    api_free(param_hdr);
}

void snat_conf_destroy(void *arg)
{
    struct snat_pool *snat = arg;

    if (arg == NULL) {
        return;
    }

    if (snat->ip4_rr != NULL) {
        if (snat->ip4_rr->addr != NULL) {
            dpdk_free(snat->ip4_rr->addr);
        }
        dpdk_free(snat->ip4_rr);
    }

    if (snat->ip6_rr != NULL) {
        if (snat->ip6_rr->addr != NULL) {
            dpdk_free(snat->ip6_rr->addr);
        }
        dpdk_free(snat->ip6_rr);
    }

    dpdk_free(snat);
}

static void _api_snat_conf_list_destroy(struct snat_pool *snat[], int count, bool need_delete)
{
    if (snat == NULL || count == 0) {
        return;
    }

    if (need_delete) {
        for (int i = 0; i < count; i++) {
            snat_conf_destroy(snat[i]);
            snat[i] = NULL;
        }
    }

    dpdk_free(snat);
}

static void _api_snat_hdr_ip4_free(struct api_snat_hdr *hdr)
{
    if (hdr == NULL || hdr->ip4_info_count == 0) {
        return;
    }

    for (int i = 0; i < hdr->cpu_count; i++) {
        api_free(hdr->ip4_info[i]);
        hdr->ip4_info[i] = NULL;

        ip4_conf_table_destroy(hdr->ip4_table[i]);
        hdr->ip4_table[i] = NULL;
    }
}

static void _api_snat_hdr_ip6_free(struct api_snat_hdr *hdr)
{
    if (hdr == NULL || hdr->ip6_info_count == 0) {
        return;
    }

    for (int i = 0; i < hdr->cpu_count; i++) {
        api_free(hdr->ip6_info[i]);
        hdr->ip6_info[i] = NULL;

        ip6_conf_table_destroy(hdr->ip6_table[i]);
        hdr->ip6_table[i] = NULL;
    }
}

static void _api_snat_hdr_pool_free(struct api_snat_hdr *hdr)
{
    if (hdr == NULL || hdr->snat_count == 0) {
        return;
    }

    for (int i = 0; i < hdr->cpu_count; i++) {
        snat_conf_table_destroy(hdr->snat_table[i]);
        hdr->snat_table[i] = NULL;

        _api_snat_conf_list_destroy(hdr->snat[i], hdr->snat_count, hdr->need_delete);
        hdr->snat[i] = NULL;
    }
}

static void _api_snat_hdr_free(struct api_snat_hdr *hdr)
{
    if (hdr == NULL) {
        return;
    }

    _api_snat_hdr_ip4_free(hdr);
    _api_snat_hdr_ip6_free(hdr);
    _api_snat_hdr_pool_free(hdr);

    api_free(hdr);
}

static void _api_snat_pool_free(struct snat_pool *snat)
{
    if (snat == NULL) {
        return;
    }

    if (snat->ip4_rr != NULL) {
        if (snat->ip4_rr->addr != NULL) {
            dpdk_free(snat->ip4_rr->addr);
            snat->ip4_rr->addr = NULL;
        }

        dpdk_free(snat->ip4_rr);
        snat->ip4_rr = NULL;
    }

    if (snat->ip6_rr != NULL) {
        if (snat->ip6_rr->addr != NULL) {
            dpdk_free(snat->ip6_rr->addr);
            snat->ip6_rr->addr = NULL;
        }

        dpdk_free(snat->ip6_rr);
        snat->ip6_rr = NULL;
    }

    dpdk_free(snat);
}

static void *_api_snat_pool_alloc(int ip4_count, int ip6_count, int hw_numa_id)
{
    uint32_t *ip4_addr = NULL;
    struct snat_pool *snat = NULL;
    struct snat_ip4_rr *ip4_rr = NULL;
    struct snat_ip6_rr *ip6_rr = NULL;
    struct dpdk_ip6_addr *ip6_addr = NULL;

    snat = api_malloc_numa(sizeof(*snat), hw_numa_id);
    if (snat == NULL) {
        return NULL;
    }

    if (ip4_count != 0) {
        ip4_rr = snat->ip4_rr = api_malloc_numa(sizeof(*snat->ip4_rr), hw_numa_id);
        if (ip4_rr == NULL) {
            goto _quit;
        }

        ip4_rr->count = ip4_count;
        ip4_addr = ip4_rr->addr = api_malloc_numa(ip4_count * sizeof(uint32_t), hw_numa_id);
        if (ip4_addr == NULL) {
            goto _quit;
        }
    }

    if (ip6_count != 0) {
        ip6_rr = snat->ip6_rr = api_malloc_numa(sizeof(*snat->ip6_rr), hw_numa_id);
        if (ip6_rr == NULL) {
            goto _quit;
        }

        ip6_rr->count = ip6_count;
        ip6_addr = ip6_rr->addr = api_malloc_numa(ip6_count * sizeof(struct dpdk_ip6_addr), hw_numa_id);
        if (ip6_addr == NULL) {
            goto _quit;
        }
    }

    return snat;

_quit:
    _api_snat_pool_free(snat);
    return NULL;
}

static struct api_snat_hdr *_api_snat_hdr_malloc(int cpu_count, bool need_delete)
{
    struct api_snat_hdr *snat_hdr = NULL;

    snat_hdr = api_malloc(sizeof(*snat_hdr));
    if (snat_hdr == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(snat_hdr, 0, sizeof(*snat_hdr));
    snat_hdr->cpu_count = cpu_count;
    snat_hdr->need_delete = need_delete;

    return snat_hdr;
}

static enum SNAT_ADDR_POLICY _api_snat_string_to_enum(const char *policy)
{
    if (policy == NULL) {
        return SNAT_ADDR_INVALID;
    }

    for (size_t i = 0; i < ARR_NUMS(s_snat_addr_policy_name); i++) {
        if (strcmp(policy, s_snat_addr_policy_name[i]) == 0) {
            return i;
        }
    }

    LOG_ERROR("Snat policy (%s) not support", policy);
    return SNAT_ADDR_INVALID;
}

static int _api_snat_iface_parse(uint8_t *port, void *obj)
{
    uint8_t dst = 0;
    const char *interface = NULL;

    interface = api_json_get_string(obj, "interface");
    if (interface == NULL) {
        return ERRCODE_PARAMETER_INVALID;
    }

    dst = dpdk_port_by_name_get(interface);
    if (dst == UINT8_MAX) {
        return ERRCODE_PORT_NOT_EXIST;
    }

    *port = dst;
    return 0;
}

static int _api_snat_addr_policy_parse(int *policy, void *obj)
{
    int v = 0;
    const char *str = NULL;

    str = api_json_get_string(obj, "addr-policy");
    if (str == NULL) {
        return ERRCODE_PARAMETER_INVALID;
    }

    v = _api_snat_string_to_enum(str);
    if (v == SNAT_ADDR_INVALID) {
        return ERRCODE_PARAMETER_INVALID;
    }

    *policy = v;
    return 0;
}

static int _api_snat_addr_info_extend(struct addr_info_hdr *hdr)
{
    int cap = 0;
    void *ptr = NULL;

    if (hdr->cap != hdr->count) {
        return 0;
    }

    cap = hdr->cap + API_SNAT_ADDR_BASE_COUNT;
    ptr = api_realloc(hdr->infos, cap * sizeof(*hdr->infos));
    if (ptr == NULL) {
        return ERRCODE_OOM;
    }

    hdr->cap = cap;
    hdr->infos = ptr;

    return 0;
}

static struct addr_info *_api_snat_ip_find(struct addr_info_entry *entry, int af, const union inet_addr *addr)
{
    struct addr_info *target = NULL;

    for (int i = 0; i < entry->count; i++) {
        target = &entry->info[i];
        if (target->af != af) {
            continue;
        }

        if (target->af == AF_INET) {
            if (target->addr.ip != addr->ip) {
                continue;
            }
        } else {
            if (dpdk_ip6_addr_cmp(&target->addr, addr, sizeof(*addr)) != 0) {
                continue;
            }
        }

        return target;
    }

    return NULL;
}

static int _api_snat_ip4_expand(struct api_param *param, uint32_t start_addr, uint32_t end_addr)
{
    int code = 0;
    union inet_addr addr = {0};
    struct addr_info *info = NULL;
    struct addr_info_hdr *hdr = &param->v4_hdr;
    struct addr_info_entry *entry = hdr->entry;

    for (uint32_t i = start_addr; i <= end_addr; i++) {
        code = _api_snat_addr_info_extend(hdr);
        if (code != 0) {
            return code;
        }

        addr.ip = dpdk_cpu_to_be_32(i);
        info = _api_snat_ip_find(entry, AF_INET, &addr);
        if (info == NULL) {
            if (hdr->count >= API_SNAT_ADDR_MAX) {
                LOG_ERROR("The total number of IPs exceeds the threshold.");
                return ERRCODE_IP_LIMIT_EXCEEDED;
            }

            info = &entry->info[entry->count++];
            info->af = AF_INET;
            info->addr = addr;
            info->port = param->port;
        }

        info->refcnt += 1;
        hdr->infos[hdr->count++] = info;
    }

    return 0;
}

static int _api_snat_addr_info_add(struct addr_info_hdr *hdr, uint16_t port, const struct dpdk_ip6_addr *addr)
{
    int code = 0;
    struct addr_info *info = NULL;
    struct addr_info_entry *entry = hdr->entry;

    code = _api_snat_addr_info_extend(hdr);
    if (code != 0) {
        return code;
    }

    info = _api_snat_ip_find(entry, AF_INET6, (const union inet_addr *)addr);
    if (info == NULL) {
        if (entry->count >= API_SNAT_ADDR_MAX) {
            LOG_ERROR("The total number of IPs exceeds the threshold.");
            return ERRCODE_IP_LIMIT_EXCEEDED;
        }

        info = &entry->info[entry->count++];
        info->af = AF_INET6;
        info->addr = *(const union inet_addr *)addr;
        info->port = port;
        info->refcnt = 0;
    }

    info->refcnt += 1;
    hdr->infos[hdr->count++] = info;

    return 0;
}

static int _api_snat_ip6_expand(struct api_param *param, struct dpdk_ip6_addr *start_addr, struct dpdk_ip6_addr *end_addr)
{
    int code = 0;
    uint32_t diff = 0;
    struct dpdk_ip6_addr next = {0};
    struct dpdk_ip6_addr addr = {0};
    struct addr_info_hdr *hdr = &param->v6_hdr;

    diff = dpdk_ip6_addr_diff(start_addr, end_addr);
    if (diff > API_SNAT_ADDR_MAX) {
        LOG_ERROR("The number of IPs exceeds the threshold.");
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    if (memcmp(start_addr, end_addr, sizeof(*start_addr)) == 0) {
        code = _api_snat_addr_info_add(hdr, param->port, start_addr);
        if (code != 0) {
            return code;
        }

        return 0;
    }

    next = *start_addr;
    do {
        dpdk_ip6_addr_inc(&next, start_addr);
        code = _api_snat_addr_info_add(hdr, param->port, &addr);
        if (code != 0) {
            return code;
        }

        addr = next;
    } while (memcmp(&addr, end_addr, sizeof(*end_addr)) != 0);

    return 0;
}

static int _api_snat_addr_info_v4_parse(struct api_param *param, void *array)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    uint32_t end_addr = 0;
    uint32_t start_addr = 0;

    count = json_array_size(array);
    if (count == 0) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        const char *str0 = NULL;
        const char *str1 = NULL;

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            return ERRCODE_PARAMETER_INVALID;
        }

        str0 = api_json_get_string(obj, "start_addr");
        if (str0 == NULL) {
            return ERRCODE_PARAMETER_INVALID;
        }

        str1 = api_json_get_string(obj, "end_addr");
        if (str1 == NULL) {
            return ERRCODE_PARAMETER_INVALID;
        }

        inet_pton(AF_INET, str0, &start_addr);
        inet_pton(AF_INET, str1, &end_addr);

        code = _api_snat_ip4_expand(param, dpdk_be_to_cpu_32(start_addr), dpdk_be_to_cpu_32(end_addr));
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_snat_addr_info_v6_parse(struct api_param *param, void *array)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    struct dpdk_ip6_addr end_addr = {0};
    struct dpdk_ip6_addr start_addr = {0};

    count = json_array_size(array);
    if (count == 0) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        const char *str0 = NULL;
        const char *str1 = NULL;

        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            LOG_ERROR("Invalid parameter.");
            return ERRCODE_PARAMETER_INVALID;
        }

        str0 = api_json_get_string(obj, "start_addr");
        if (str0 == NULL) {
            return ERRCODE_PARAMETER_INVALID;
        }

        str1 = api_json_get_string(obj, "end_addr");
        if (str1 == NULL) {
            return ERRCODE_PARAMETER_INVALID;
        }

        inet_pton(AF_INET6, str0, &start_addr);
        inet_pton(AF_INET6, str1, &end_addr);

        code = _api_snat_ip6_expand(param, &start_addr, &end_addr);
        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_snat_addr_info_parse(struct api_param *param, void *obj)
{
    int code = 0;
    void *ip4_array = NULL;
    void *ip6_array = NULL;

    ip4_array = json_object_get(obj, "ip4_entries");
    if (ip4_array != NULL) {
        code = _api_snat_addr_info_v4_parse(param, ip4_array);
        if (code != 0) {
            goto _quit;
        }
    }

    ip6_array = json_object_get(obj, "ip6_entries");
    if (ip6_array != 0) {
        code = _api_snat_addr_info_v6_parse(param, ip6_array);
        if (code != 0) {
            goto _quit;
        }
    }

    if (ip4_array == NULL && ip6_array == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    return 0;

_quit:
    _api_snat_param_free(param);
    return code;
}

static int _api_snat_port_parse(uint16_t *min_port, uint16_t *max_port, void *obj)
{
    int code = 0;
    uint64_t port0 = 0;
    uint64_t port1 = 0;

    code = api_json_get_long(&port0, obj, "min_port");
    if (code != 0) {
        return code;
    }

    code = api_json_get_long(&port1, obj, "max_port");
    if (code != 0) {
        return code;
    }

    *min_port = (uint16_t) port0;
    *max_port = (uint16_t) port1;

    return 0;
}

static void *_api_snat_param_hdr_alloc(int count)
{
    struct api_param *param = NULL;
    struct api_param_hdr *hdr = NULL;
    struct addr_info_hdr *v4_hdr = NULL;
    struct addr_info_hdr *v6_hdr = NULL;

    hdr = api_malloc(sizeof(*hdr) + count * sizeof(struct api_param));
    if (hdr == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        param = &hdr->params[i];
        v4_hdr = &param->v4_hdr;
        v6_hdr = &param->v6_hdr;

        v4_hdr->entry = &hdr->entry;
        v6_hdr->entry = &hdr->entry;
    }

    hdr->count = count;
    return hdr;
}

static int _api_snat_post_parse(struct api_param_hdr **pp_hdr, struct root *root, void *json)
{
    int code = 0;
    size_t count = 0;
    void *obj = NULL;
    void *array = NULL;
    struct api_param *param = NULL;
    struct api_param_hdr *hdr = NULL;

    code = api_v1_modify_list(&array, &count, json, API_SNAT_MODULE_NAME, API_SNAT_LIST_NAME);
    if (code != 0) {
        return code;
    }

    if (count > DP_SNAT_POOL_MAX) {
        LOG_ERROR("SNAT pool count exceeded the threshold.");
        return ERRCODE_SNAT_POOL_EXCEED_THRESHOLD;
    }

    hdr = _api_snat_param_hdr_alloc(count);
    if (hdr == NULL) {
        return ERRCODE_OOM;
    }

    for (size_t i = 0; i < count; i++) {
        obj = api_json_array_get(array, i);
        param = &hdr->params[i];

        param->name = api_json_get_string(obj, "name");
        if (param->name == NULL) {
            code = ERRCODE_PARAMETER_INVALID;
            goto _quit;
        }

        code = _api_snat_iface_parse(&param->port, obj);
        if (code != 0) {
            goto _quit;
        }

        code = _api_snat_addr_policy_parse(&param->policy, obj);
        if (code != 0) {
            goto _quit;
        }

        code = _api_snat_addr_info_parse(param, obj);
        if (code != 0) {
            goto _quit;
        }

        code = _api_snat_port_parse(&param->min_port, &param->max_port, obj);
        if (code != 0) {
            goto _quit;
        }
    }

    *pp_hdr = hdr;
    return 0;

_quit:
    _api_snat_param_hdr_free(hdr);
    return code;
}

static int _api_snat_ip4_count(const struct addr_info_entry *entry)
{
    int count = 0;
    const struct addr_info *info = NULL;

    for (int i = 0; i < entry->count; i++) {
        info = &entry->info[i];
        if (info->af == AF_INET) {
            count += 1;
        }
    }

    return count;
}

static int _api_snat_ip6_count(const struct addr_info_entry *entry)
{
    int count = 0;
    const struct addr_info *info = NULL;

    for (int i = 0; i < entry->count; i++) {
        info = &entry->info[i];
        if (info->af == AF_INET6) {
            count += 1;
        }
    }

    return count;
}

static int _api_snat_to_ip4_table(struct api_snat_hdr *snat_hdr, const struct root *root, const struct api_param_hdr *param_hdr)
{
    int code = 0;
    uint8_t mask = 32;
    int ip4_count = 0;
    void *ip4_table = NULL;
    struct ip4_info *ip4_info = NULL;
    const struct dataplane *dp = NULL;
    const struct addr_info *addr_info = NULL;
    int cpu_count = root->hw_info.cpu_count;

    ip4_count = _api_snat_ip4_count(&param_hdr->entry);
    snat_hdr->ip4_info_count = ip4_count;
    if (ip4_count == 0) {
        return 0;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        ip4_info = snat_hdr->ip4_info[i] = dpdk_malloc_numa(ip4_count * sizeof(struct ip4_info), dp->hw_numa_id);
        if (ip4_info == NULL) {
            LOG_ERROR("OOM.");
            code = ERRCODE_OOM;
            goto _quit;
        }

        for (int j = 0; j < param_hdr->entry.count; j++) {
            addr_info = &param_hdr->entry.info[j];
            if (addr_info->af != AF_INET) {
                continue;
            }

            code = ip4_info_init(&ip4_info[j], addr_info->addr.ip, mask, addr_info->port, IP_MASTER, addr_info->refcnt);
            if (code != 0) {
                goto _quit;
            }
        }

        code = ip4_conf_table_create_and_append(&ip4_table, dp->tc->ip4_table, ip4_info, ip4_count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }

        snat_hdr->ip4_table[i] = ip4_table;
    }

    return 0;

_quit:
    _api_snat_hdr_ip4_free(snat_hdr);
    return code;
}

static int _api_snat_to_ip6_table(struct api_snat_hdr *snat_hdr, const struct root *root, const struct api_param_hdr *param_hdr)
{
    int n = 0;
    int code = 0;
    int ip6_count = 0;
    uint8_t mask = 128;
    void *ip6_table = NULL;
    struct ip6_info *ip6_info = NULL;
    const struct dataplane *dp = NULL;
    const struct addr_info *addr_info = NULL;
    int cpu_count = root->hw_info.cpu_count;

    ip6_count = _api_snat_ip6_count(&param_hdr->entry);
    snat_hdr->ip6_info_count = ip6_count;
    if (ip6_count == 0) {
        return 0;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        ip6_info = snat_hdr->ip6_info[i] = dpdk_malloc_numa(ip6_count * sizeof(struct ip6_info), dp->hw_numa_id);
        if (ip6_info == NULL) {
            LOG_ERROR("OOM.");
            code = ERRCODE_OOM;
            goto _quit;
        }

        for (int j = 0; j < param_hdr->entry.count; j++) {
            addr_info = &param_hdr->entry.info[j];
            if (addr_info->af != AF_INET6) {
                continue;
            }

            code = ip6_info_init(&ip6_info[n++], &addr_info->addr.addr, mask, addr_info->port, IP_MASTER, addr_info->refcnt);
            if (code != 0) {
                goto _quit;
            }
        }

        code = ip6_conf_table_create_and_append(&ip6_table, dp->tc->ip4_table, ip6_info, ip6_count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }

        snat_hdr->ip6_table[i] = ip6_table;
    }

    return 0;

_quit:
    _api_snat_hdr_ip6_free(snat_hdr);
    return code;
}

static struct snat_pool *_api_snat_param_to_pool(const struct api_param *param, int hw_numa_id)
{
    struct snat_pool *snat = NULL;
    struct snat_ip4_rr *ip4_rr = NULL;
    struct snat_ip6_rr *ip6_rr = NULL;
    const struct addr_info_hdr *ip4_info = &param->v4_hdr;
    const struct addr_info_hdr *ip6_info = &param->v6_hdr;

    snat = _api_snat_pool_alloc(ip4_info->count, ip6_info->count, hw_numa_id);
    if (snat == NULL) {
        return NULL;
    }

    snat->id = SNAT_ADDR_INVALID;
    snat->port = param->port;
    snat->snat_ip4_get_next = NULL;
    snat->snat_ip6_get_next = NULL;

    for (int i = 0; i < ip4_info->count; i++) {
        ip4_rr = snat->ip4_rr;
        ip4_info = &param->v4_hdr;

        ip4_rr->count = ip4_rr->count;
        ip4_rr->next = 0;

        for (int j = 0; j < ip4_rr->count; j++) {
            ip4_rr->addr[j] = ip4_info->infos[j]->addr.ip;
        }

        ip4_rr->min_port = param->min_port;
        ip4_rr->max_port = param->max_port;
    }

    for (int i = 0; i < ip6_info->count; i++) {
        ip6_rr = snat->ip6_rr;;
        ip6_info = &param->v6_hdr;

        ip6_rr->count = ip6_rr->count;
        ip6_rr->next = 0;

        for (int j = 0; j < ip6_rr->count; j++) {
            dpdk_memcpy(&ip6_rr->addr[j], &ip6_info->infos[j]->addr, sizeof(ip6_rr->addr[j]));
        }

        ip6_rr->min_port = param->min_port;
        ip6_rr->max_port = param->max_port;
    }

    snat->refcnt = 1;
    snat->policy = param->policy;
    dpdk_memcpy(snat->name, param->name, strlen(param->name));

    return snat;
}

static int _api_snat_to_table(struct api_snat_hdr *snat_hdr, const struct root *root,
                              const struct api_param_hdr *param_hdr)
{
    int code = 0;
    int hw_numa_id = 0;
    void *table = NULL;
    struct dataplane *dp = NULL;
    struct snat_pool *snat = NULL;
    struct snat_pool **pp_snat = NULL;
    const struct api_param *param = NULL;
    int cpu_count = root->hw_info.cpu_count;

    snat_hdr->snat_count = param_hdr->count;

    for (int i = 0; i < cpu_count; i++) {
        pp_snat = snat_hdr->snat[i] = api_malloc(snat_hdr->snat_count * sizeof(*snat_hdr->snat));
        if (pp_snat == NULL) {
            code = ERRCODE_OOM;
            goto _quit;
        }

        dp = root->dpdk_thread[i];
        hw_numa_id = dp->hw_numa_id;

        for (int j = 0; j < snat_hdr->snat_count; j++) {
            param = &param_hdr->params[j];
            snat = pp_snat[j] = _api_snat_param_to_pool(param, hw_numa_id);
            if (snat == NULL) {
                goto _quit;
            }
        }

        code = snat_conf_table_append(&table, dp->tc->snat_table, pp_snat, snat_hdr->snat_count, dp->hw_numa_id);
        if (code != 0) {
            goto _quit;
        }

        snat_hdr->snat_table[i] = table;
    }

_quit:
    return code;
}

static int _api_snat_del(struct api_snat_hdr **pp_snat_hdr, const struct root *root, const struct api_param_hdr *param_hdr)
{
    // int code = 0;
    struct api_snat_hdr *snat_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    snat_hdr = _api_snat_hdr_malloc(cpu_count, false);
    if (snat_hdr == NULL) {
        return ERRCODE_OOM;
    }

    /*code = _api_snat_to_ip4_table(snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_to_ip6_table(snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_to_table(snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }*/

    snat_hdr->need_delete = true;
    *pp_snat_hdr = snat_hdr;
    return 0;

/*_quit:
    _api_snat_hdr_free(snat_hdr);
    return code;*/
}

static int _api_snat_add(struct api_snat_hdr **psnat_hdr, const struct root *root, struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_snat_hdr *snat_hdr = NULL;
    int cpu_count = root->hw_info.cpu_count;

    snat_hdr = _api_snat_hdr_malloc(cpu_count, true);
    if (snat_hdr == NULL) {
        return ERRCODE_OOM;
    }

    code = _api_snat_to_ip4_table(snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_to_ip6_table(snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_to_table(snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    snat_hdr->need_delete = false;
    *psnat_hdr = snat_hdr;
    return 0;

_quit:
    _api_snat_hdr_free(snat_hdr);
    return code;
}

static void _api_snat_update(struct root *root, const struct api_snat_hdr *hdr)
{
    struct dataplane *dp = NULL;
    int cpu_count = hdr->cpu_count;
    void **position[CPU_MAX] = {NULL};

    // ip4 table
    if (hdr->ip4_table[0] != NULL) {
        for (int i = 0; i < cpu_count; i++) {
            dp = root->dpdk_thread[i];
            position[i] = &dp->tc->ip4_table;
        }

        api_thread_config_update(root, position, (void **)hdr->ip4_table, ip4_conf_table_destroy);
    }

    // ip6 table
    if (hdr->ip6_table[0] != NULL) {
        for (int i = 0; i < cpu_count; i++) {
            dp = root->dpdk_thread[i];
            position[i] = &dp->tc->ip6_table;
        }

        api_thread_config_update(root, position, (void **)hdr->ip6_table, ip6_conf_table_destroy);
    }

    // snat pool table
    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        position[i] = &dp->tc->snat_table;
    }

    api_thread_config_update(root, position, (void **)hdr->snat_table, snat_conf_table_destroy);
}

static int _api_snat_ip4_policy_to_json(void *obj, const void *ptr)
{
    int code = 0;
    void *array = NULL;
    void *subobj = NULL;
    uint32_t end_addr = 0;
    uint32_t start_addr = 0;
    char buffer[CACHE_LINE] = "";
    const struct snat_ip4_rr *ip4_rr = (const struct snat_ip4_rr *) ptr;

    code = api_json_array(&array);
    if (code != 0) {
        goto _quit;
    }

    code = api_json_object(&subobj);
    if (code != 0) {
        goto _quit;
    }

    start_addr = ip4_rr->addr[0];
    end_addr = ip4_rr->addr[ip4_rr->count - 1];

    inet_ntop(AF_INET, &start_addr, buffer, sizeof(buffer));
    code = api_json_add_string(subobj, "start_addr", buffer);
    if (code != 0) {
        goto _quit;
    }

    inet_ntop(AF_INET, &end_addr, buffer, sizeof(buffer));
    code = api_json_add_string(subobj, "end_addr", buffer);
    if (code != 0) {
        goto _quit;
    }

    code = api_json_array_append(array, subobj);
    if (code != 0) {
        goto _quit;
    }
    subobj = NULL;

    code = api_json_add_object(obj, "ip4_entries", array);
    if (code != 0) {
        goto _quit;
    }

    return 0;

_quit:
    api_json_free(array);
    api_json_free(subobj);
    return code;
}

static int _api_snat_ip6_policy_to_json(void *obj, const void *ptr)
{
    int code = 0;
    void *subobj = NULL;
    void *array = NULL;
    char buffer[CACHE_LINE] = "";
    struct dpdk_ip6_addr *end_addr = NULL;
    struct dpdk_ip6_addr *start_addr = NULL;
    const struct snat_ip6_rr *ip6_rr = (const struct snat_ip6_rr *) ptr;

    code = api_json_array(&array);
    if (code != 0) {
        goto _quit;
    }

    code = api_json_object(&subobj);
    if (code != 0) {
        goto _quit;
    }

    start_addr = &ip6_rr->addr[0];
    end_addr = &ip6_rr->addr[ip6_rr->count - 1];

    inet_ntop(AF_INET6, start_addr, buffer, sizeof(buffer));
    code = api_json_add_string(subobj, "start_addr", buffer);
    if (code != 0) {
        goto _quit;
    }

    inet_ntop(AF_INET6, end_addr, buffer, sizeof(buffer));
    code = api_json_add_string(subobj, "end_addr", buffer);
    if (code != 0) {
        goto _quit;
    }

    code = api_json_array_append(array, subobj);
    if (code != 0) {
        goto _quit;
    }
    subobj = NULL;

    code = api_json_add_object(obj, "ip6_entries", array);
    if (code != 0) {
        goto _quit;
    }

    return 0;

_quit:
    api_json_free(array);
    api_json_free(subobj);
    return code;
}

static int _api_snat_del_v4_hdr_organize(struct addr_info_hdr *info_hdr, const struct snat_ip4_rr *ip4_rr, uint8_t port)
{
    union inet_addr addr = {0};
    struct addr_info *info = NULL;
    uint32_t count = ip4_rr->count;
    struct addr_info_entry *entry = info_hdr->entry;

    info_hdr->infos = api_malloc(count * sizeof(*info_hdr->infos));
    if (info_hdr->infos == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < ip4_rr->count; i++) {
        addr.ip = ip4_rr->addr[i];
        info = _api_snat_ip_find(info_hdr->entry, AF_INET, &addr);
        if (info == NULL) {
            info = info_hdr->infos[i] = &entry->info[entry->count++];
            info->af = AF_INET;
            info->addr = addr;
            info->port = port;
            info->refcnt -= 1;
        } else {
            info->refcnt -= 1;
            info_hdr->infos[i] = info;
        }
    }

    return 0;
}

static int _api_snat_del_v6_hdr_organize(struct addr_info_hdr *info_hdr, const struct snat_ip6_rr *ip6_rr, uint8_t port)
{
    union inet_addr addr = {0};
    struct addr_info *info = NULL;
    uint32_t count = ip6_rr->count;
    struct addr_info_entry *entry = info_hdr->entry;

    info_hdr->infos = api_malloc(count * sizeof(*info_hdr->infos));
    if (info_hdr->infos == NULL) {
        return ERRCODE_OOM;
    }

    for (int i = 0; i < ip6_rr->count; i++) {
        addr = *(union inet_addr *)&ip6_rr->addr[i];
        info = _api_snat_ip_find(info_hdr->entry, AF_INET6, &addr);
        if (info == NULL) {
            info = info_hdr->infos[i] = &entry->info[entry->count++];
            info->af = AF_INET6;
            info->addr = addr;
            info->port = port;
            info->refcnt -= 1;
        } else {
            info->refcnt -= 1;
            info_hdr->infos[i] = info;
        }
    }

    return 0;
}

static int _api_snat_del_organize(struct api_param_hdr *param_hdr)
{
    int code = 0;
    struct api_param *param = NULL;
    const struct snat_pool *snat = NULL;
    struct addr_info_hdr *v4_hdr = NULL;
    struct addr_info_hdr *v6_hdr = NULL;
    const struct snat_ip4_rr *ip4_rr = NULL;
    const struct snat_ip6_rr *ip6_rr = NULL;

    for (int i = 0; i < param_hdr->count; i++) {
        param = &param_hdr->params[i];
        v4_hdr = &param->v4_hdr;
        v6_hdr = &param->v6_hdr;

        snat = param->snat;
        ip4_rr = snat->ip4_rr;
        ip6_rr = snat->ip6_rr;

        if (ip4_rr != NULL) {
            code = _api_snat_del_v4_hdr_organize(v4_hdr, snat->ip4_rr, snat->port);
        }

        if (ip6_rr != NULL) {
            code = _api_snat_del_v6_hdr_organize(v6_hdr, snat->ip6_rr, snat->port);
        }

        if (code != 0) {
            return code;
        }
    }

    return 0;
}

static int _api_snat_del_parse(struct api_param_hdr **pp_hdr, struct root *root, void *json)
{
    int code = 0;
    void *obj = NULL;
    size_t count = 0;
    void *array = NULL;
    const char *name = NULL;
    struct snat_pool *snat = NULL;
    struct api_param_hdr *param_hdr = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_v1_delete_list(&array, &count, json, API_SNAT_MODULE_NAME, API_SNAT_LIST_NAME);
    if (code != 0) {
        goto _quit;
    }

    *pp_hdr = param_hdr = _api_snat_param_hdr_alloc(count);
    if (param_hdr == NULL) {
        goto _quit;
    }

    for (size_t i = 0; i < count; i++) {
        obj = api_json_array_get(array, i);
        if (obj == NULL) {
            LOG_ERROR("Invalid parameter.");
            goto _quit;
        }

        name = api_json_get_string(obj, "name");
        if (name == NULL) {
            LOG_ERROR("Invalid parameter.");
            goto _quit;
        }

        code = snat_conf_get_by_name(dp->tc->snat_table, &snat, name);
        if (code != 0) {
            goto _quit;
        }

        param_hdr->params[i].snat = snat;
    }

    code = _api_snat_del_organize(param_hdr);
    if (code != 0) {
        goto _quit;
    }

    return 0;

_quit:
    _api_snat_param_hdr_free(param_hdr);
    return code;
}

static int _api_snat_query(void *array, struct snat_pool *snats[], int count)
{
    int code = 0;
    void *obj = NULL;
    bool has_port = false;
    const struct snat_pool *snat = NULL;
    const struct snat_ip4_rr *ip4_rr = NULL;
    const struct snat_ip6_rr *ip6_rr = NULL;

    for (int i = 0; i < count; i++) {
        snat = snats[i];
        code = api_json_object(&obj);
        if (code != 0) {
            goto _quit;
        }

        code = api_json_add_string(obj, "name", snat->name);
        if (code != 0) {
            goto _quit;
        }

        code = api_json_add_string(obj, "policy", s_snat_addr_policy_name[snat->policy]);
        if (code != 0) {
            goto _quit;
        }

        if (snat->ip4_rr != NULL) {
            ip4_rr = snat->ip4_rr;
            code = _api_snat_ip4_policy_to_json(obj, ip4_rr);
            if (code != 0) {
                goto _quit;
            }

            if (!has_port) {
                has_port = true;

                code = api_json_add_long(obj, "min_port", ip4_rr->min_port);
                if (code != 0) {
                    goto _quit;
                }

                code = api_json_add_long(obj, "max_port", ip4_rr->max_port);
                if (code != 0) {
                    goto _quit;
                }
            }
        }

        if (snat->ip6_rr != NULL) {
            ip6_rr = snat->ip6_rr;
            code = _api_snat_ip6_policy_to_json(obj, ip6_rr);
            if (code != 0) {
                goto _quit;
            }

            if (!has_port) {
                has_port = true;

                code = api_json_add_long(obj, "min_port", ip6_rr->min_port);
                if (code != 0) {
                    goto _quit;
                }

                code = api_json_add_long(obj, "max_port", ip6_rr->max_port);
                if (code != 0) {
                    goto _quit;
                }
            }
        }

        code = api_json_array_append(array, obj);
        if (code != 0) {
            goto _quit;
        }
    }

    return 0;

_quit:
    api_json_free(obj);
    return code;
}

API_POST(/v1/network/snat_pool, snat_pool)
{
    int code = 0;
    struct root *root = cfg;
    struct api_snat_hdr *snat_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_snat_post_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_add(&snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_snat_update(root, snat_hdr);

_quit:
    _api_snat_param_hdr_free(param_hdr);
    _api_snat_hdr_free(snat_hdr);

    if (code != 0) {
        return api_fail(code);
    }
    return api_succ(NULL);
}

API_PUT(/v1/network/snat_pool, snat_pool)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/network/snat_pool, snat_pool)
{
    int code = 0;
    struct root *root = cfg;
    struct api_snat_hdr *snat_hdr = NULL;
    struct api_param_hdr *param_hdr = NULL;

    code = _api_snat_del_parse(&param_hdr, root, json);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_del(&snat_hdr, root, param_hdr);
    if (code != 0) {
        goto _quit;
    }

    _api_snat_update(root, snat_hdr);

_quit:
    _api_snat_param_hdr_free(param_hdr);
    _api_snat_hdr_free(snat_hdr);
    if (code != 0) {
        return api_fail(code);
    }

    return api_succ(NULL);
}

API_GET(/v1/network/snat_pool, snat_pool)
{
    int code = 0;
    int count = 0;
    void *array = NULL;
    struct root *root = cfg;
    void *snat_table = NULL;
    struct snat_pool **pp_snat = NULL;
    struct dataplane *dp = root->dpdk_thread[0];

    code = api_json_array(&array);
    if (code != 0) {
        return api_fail(code);
    }

    snat_table = dp->tc->snat_table;
    code = snat_conf_table_get_count(snat_table, &count);
    if (code != 0) {
        goto _quit;
    }

    if (count == 0) {
        goto _quit;
    }

    pp_snat = api_malloc(count * sizeof(*pp_snat));
    if (pp_snat == NULL) {
        code = ERRCODE_OOM;
        goto _quit;
    }

    code = snat_conf_table_get_element(snat_table, pp_snat, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_snat_query(array, pp_snat, count);

_quit:
    api_free(pp_snat);

    if (code != 0) {
        api_json_free(array);
        return api_fail(code);
    }
    return api_succ(array);
}