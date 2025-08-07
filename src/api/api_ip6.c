/*****************************************************************************
 * filename: api_ipv6.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "l3.h"
#include "log.h"
#include "route6.h"
#include "errcode.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_common.h"

struct api_ip6 {
    const char *name;
    uint16_t port;
    int mask;
    enum IP_TYPE type;
    union dpdk_ipv6_addr ipv6;
    struct dpdk_mac mac;
};

static int s_ndp_thread_id = -1;

static INLINE void _api_ip6_ndp_free(void **ndp, int count)
{
    // dpdk_pktmbuf_push(ndp, count);
}

static void _api_ip6_free(void *ptr)
{
    if (ptr != NULL) {
        dpdk_free(ptr);
    }
}

static void *_api_ip6_alloc(size_t size)
{
    void *tmp = NULL;

    tmp = dpdk_malloc(size);
    if (tmp == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(tmp, 0, size);
    return tmp;
}

static int _api_ip6_post_parse_count(void *json)
{
    void *array = NULL;

    array = api_v1_modify_list(json, "ip6", "entrys");
    return json_array_size(array);
}

static int _api_ip6_post_parse(struct root *root, void *json, struct api_ip6 ip6[], int count)
{
    int ret = 0;
    void *array = NULL;
    struct api_ip6 *one = NULL;

    array = api_v1_modify_list(json, "ip6", "entrys");
    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;
        const char *ip_type = NULL;

        one = &ip6[i];
        obj = json_array_get(array, i);
        one->name = json_string_value(json_object_get(obj, "name"));
        one->port = dpdk_port_by_name_get(one->name);
        if (one->port == (USHRT_MAX)-1) {
            LOG_ERROR("Not exists(%s)", one->name);
            return ERRCODE_PORT_NOT_EXIST;
        }

        ip = json_string_value(json_object_get(obj, "ip"));
        inet_pton(AF_INET6, ip, &one->ipv6);

        one->mask = json_integer_value(json_object_get(obj, "mask"));
        ret = l2_port_mac(one->port, &one->mac);
        if (ret != 0) {
            return ERRCODE_INNER;
        }

        ip_type = json_string_value(json_object_get(obj, "type"));
        if (strcmp(ip_type, "IP_MASTER") == 0) {
            one->type = IP_MASTER;
        } else {
            one->type = IP_SECONDARY;
        }

        if (!dpdk_port_is_up(one->port)) {
            LOG_ERROR("Port %d is down", one->port);
            return ERRCODE_PORT_IS_DOWN;
        }
    }

    return 0;
}

static int _api_ip6_ndp_gen(struct root *root, void *ndp[], struct api_ip6 ip6[], int count)
{
    int id = 0;
    int ret = 0;
    enum ERRCODE code = 0;
    struct api_ip6 *one = NULL;

    id = (s_ndp_thread_id + 1) % root->hw_info.cpu_count;
    s_ndp_thread_id = id;

    ret = dpdk_pktmbuf_pop(root->dpdk_thread[id]->pktmbuf_pool, ndp, count);
    if (ret != 0) {
        LOG_ERROR("Resource busy.");
        return ERRCODE_RESOURCE_BUSY;
    }

    for (int i = 0; i < count; i++) {
        one = &ip6[i];

        ret = l2_neighbor_solicitation_gen(ndp[i], one->port, &one->ipv6, &one->mac);
        if (ret != 0) {
            _api_ip6_ndp_free(ndp, count);
            return ERRCODE_INNER;
        }

        DPDK_HEADROOM(ndp[i])->type = PKT_MBUF_NDP;
    }

    return 0;
}

static int _api_ip6_ipv6_manage_add(struct root *root, void *ipv6_manage[], struct api_ip6 *ip6, int count)
{
    return 0;
}

static int _api_ip6_route6_table_add(struct root *root, void *route6[], struct api_ip6 ip6[], int count, const void *arg)
{
    return 0;
}

API_POST(/v1/network/ip6, ip6)
{
    int count = 0;
    void **ndp = NULL;
    enum ERRCODE code = 0;
    struct root *root = cfg;
    struct api_ip6 *ip6 = NULL;
    void *route6[NUMA_MAX] = {NULL};
    void *ipv6_manage[NUMA_MAX] = {NULL};

    count = _api_ip6_post_parse_count(json);
    if (count < 0) {
        LOG_ERROR("Parameter exception.");
        return api_fail(ERRCODE_INVALID);
    }

    ip6 = _api_ip6_alloc(count * sizeof(*ip6));
    if (ip6 == NULL) {
        goto _quit;
    }

    ndp = _api_ip6_alloc(count * sizeof(*ndp));
    if (ndp == NULL) {
        goto _quit;
    }

    code = _api_ip6_post_parse(cfg, json, ip6, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_ndp_gen(cfg, ndp, ip6, count);
    if (code != 0) {
        goto _quit;
    }

    code = _api_ip6_ipv6_manage_add(cfg, ipv6_manage, ip6, count);
    if (code != 0) {
        _api_ip6_ndp_free(ndp, count);
        goto _quit;
    }

    code = _api_ip6_route6_table_add(cfg, route6, ip6, count, ipv6_manage[0]);
    if (code != 0) {
        _api_ip6_ndp_free(ndp, count);
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {

    }

    return api_succ(NULL);

_quit:
    _api_ip6_free(ip6);
    _api_ip6_free(ndp);
    return api_fail(code);
}

API_PUT(/v1/network/ip6, ip6)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DELETE(/v1/network/ip6, ip6)
{
    return api_succ(NULL);
}

API_GET(/v1/network/ip6, ip6)
{
    return api_succ(NULL);
}