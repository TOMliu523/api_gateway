/*****************************************************************************
 * filename: api_rserver.c
 * function:
 * description:
 ****************************************************************************/

#include <arpa/inet.h>
#include <linux/netfilter.h>

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "api_inner.h"
#include "dpdk_common.h"

#define API_RS_MODULE_NAME "rserver"
#define API_RS_LIST_NAME "entrys"

struct api_rserver {
    int nums;
    struct rs {
        union nf_inet_addr addr;
        int af;
        uint16_t port;
    } __attribute__((aligned(8))) rs[];
};

static void _api_rs_free(void *ptr)
{
    if (ptr != NULL) {
        dpdk_free(ptr);
    }
}

static struct api_rserver *_api_rs_alloc(int count)
{
    size_t size = 0;
    struct api_rserver *rs = NULL;

    size = sizeof(*rs) + count * sizeof(struct rs);
    rs = dpdk_malloc(size);
    if (rs == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(rs, 0, size);
    return rs;
}

static int _api_rs_parse_one(struct rs *rs, void *one)
{
    int ret = 0;
    int port = 0;
    const char *ip = NULL;

    rs->port = json_integer_value(json_object_get(one, "port"));
    ip = json_string_value(json_object_get(one, "ip"));
    rs->af = (strchr(ip, ':')) ? AF_INET6 : AF_INET;

    rs->port = port;
    ret = inet_pton(rs->af, ip, &rs->addr);
    if (ret != 1) {
        LOG_ERROR("Parameter ip addr(%s)", ip);
        return ERRCODE_INVALID;
    }

    return 0;
}

static int _api_rs_parse(struct api_rserver **output, void *json)
{
    int count = 0;
    void *array = NULL;
    enum ERRCODE code = 0;
    struct api_rserver *rs = NULL;

    array = api_v1_modify_list(json, API_RS_MODULE_NAME, API_RS_LIST_NAME);
    if (array == NULL) {
        LOG_ERROR("Parameter invalid.");
        return ERRCODE_INVALID;
    }

    count = json_array_size(array);
    if (count <= 0) {
        LOG_ERROR("Parameter invalid.");
        return ERRCODE_INVALID;
    }

    rs = _api_rs_alloc(count);
    if (rs == NULL) {
        LOG_ERROR("OOM");
        return ERRCODE_OOM;
    }

    for (int i = 0; i < count; i++) {
        void *one = NULL;

        one = json_array_get(json, i);
        code = _api_rs_parse_one(&rs->rs[i], one);
        if (code != 0) {
            goto _quit;
        }
    }

    rs->nums = count;
    *output = rs;

    return 0;

_quit:
    _api_rs_free(rs);
    return code;
}

API_POST(/v1/app/rserver, rserver)
{
    enum ERRCODE code = 0;
    // struct root *root = cfg;
    struct api_rserver *rs = NULL;

    code = _api_rs_parse(&rs, json);
    if (code != 0) {
        return api_fail(code);
    }



    return api_succ(NULL);
}

API_PUT(/v1/app/rserver, rserver)
{
    return api_fail(ERRCODE_NOT_SUPPORT);
}

API_DEL(/v1/app/rserver, rserver)
{
    return api_succ(NULL);
}

API_GET(/v1/app/rserver, rserver)
{
    return api_succ(NULL);
}