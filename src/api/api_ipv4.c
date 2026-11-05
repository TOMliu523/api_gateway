/*****************************************************************************
 * filename: api_ipv4.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>
#include <sysrepo.h>
#include <arpa/inet.h>
#include <libyang/libyang.h>

#include "top.h"
#include "log.h"
#include "api.h"

#define API_IPv4_NAME_LEN 16
#define API_IPv4_MODULE_NAME "ipv4"
#define API_IPv4_LIST_NAME "interface"

struct interface_address {
    uint32_t ip;
    uint8_t mask;
};

struct ipv4_interface {
    char name[API_IPv4_NAME_LEN];

    int count;
    struct interface_address *address;
};

struct api_ipv4 {
    int count;
    struct ipv4_interface iface[];
};

static INLINE void _api_ipv4_fini(struct api_ipv4 *ipv4)
{
    if (ipv4 == NULL || ipv4->count == 0) {
        return;
    }

    for (int i = 0; i < ipv4->count; i++) {
        struct ipv4_interface *iface = &ipv4->iface[i];

        if (iface->address != NULL) {
            free(iface->address);
            iface->address = NULL;
        }
    }

    free(ipv4);
}

static struct api_ipv4 *_api_ipv4_parse(void *param)
{
    int ret = 0;
    int total = 0;
    size_t count = 0;
    void *json = NULL;
    void *array = NULL;
    struct api_ipv4 *ipv4 = NULL;

    json = api_param_get_input(param);
    ret = api_json_v1_update_list(json, "ipv4", "interface", &array, &count);
    if (ret != 0) {
        api_param_set_error(param, ret);
        return NULL;
    }

    total = sizeof(*ipv4) + count * sizeof(struct api_ipv4);
    ipv4 = malloc(total);
    if (ipv4 == NULL) {
        LOG_ERROR("OOM");
        api_param_set_error(param, API_ERRCODE_OOM);
        return NULL;
    }

    memset(ipv4, 0, total);

    for (int i = 0; i < count; i++) {
        int address_count = 0;
        const char *name = NULL;
        void *address_array = NULL;
        void *one = json_array_get(array, i);
        struct ipv4_interface *iface = &ipv4->iface[i];

        ret = api_json_get(one, "name", (const void **)&name, JSON_STRING);
        if (ret != 0) {
            api_param_set_error(param, ret);
            goto _quit;
        }

        if (strlen(name) >= API_IPv4_NAME_LEN) {
            LOG_ERROR("Inner error");
            api_param_set_error(param, ret);
            goto _quit;
        }

        strcpy(iface->name, name);
        ret = api_json_get(one, "address", (const void **)&address_array, JSON_ARRAY);
        if (ret != 0) {
            api_param_set_error(param, ret);
            goto _quit;
        }

        address_count = json_array_size(address_array);
        iface->address = malloc(sizeof(*iface->address) * address_count);
        if (iface->address == NULL) {
            LOG_ERROR("OOM");
            api_param_set_error(param, API_ERRCODE_OOM);
            goto _quit;
        }

        memset(iface->address, 0, sizeof(*iface->address) * address_count);
        ipv4->count = address_count;

        for (int j = 0; j < address_count; j++) {
            char *ip_value = NULL;
            json_int_t prefix_value = 0;
            void *addr_json = json_array_get(address_array, j);
            struct interface_address *address = &iface->address[j];

            ret = api_json_get(addr_json, "ip", (const void **)&ip_value, JSON_STRING);
            if (ret != 0) {
                api_param_set_error(param, ret);
                goto _quit;
            }

            ret = api_json_get(addr_json, "prefix-length", (const void **)&prefix_value, JSON_INTEGER);
            if (ret != 0) {
                api_param_set_error(param, ret);
                goto _quit;
            }

            address->mask = (uint8_t)prefix_value;
            ret = inet_pton(AF_INET, ip_value, &address->ip);
            if (ret != 1) {
                LOG_ERROR("IPv4 address format error.");
                goto _quit;
            }
        }

        ipv4->count += 1;
    }

    return ipv4;

_quit:
    _api_ipv4_fini(ipv4);
    return NULL;
}

static void *_api_ipv4_get(void *param, sr_data_t *data)
{
    LY_ERR err = 0;
    char *json = NULL;
    json_t *root = NULL;
    const char *errmsg = NULL;

    err = lyd_print_mem(&json, data->tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS);
    if (err != 0) {
        errmsg = ly_strerr(err);
        LOG_ERROR("Function(lyd_print_mem) failure: %s", errmsg);
        api_param_set_error(param, API_ERRCODE_CONFIG_QUERY);
        return NULL;
    }

    root = json_loads(json, 0, NULL);
    free(json);
    if (root == NULL) {
        LOG_ERROR("Function(json_loads) failure");
        api_param_set_error(param, API_ERRCODE_JSON_FORMAT);
        return NULL;
    }

    return root;
}

API_POST(ipv4, /v1/ipv4)
{
    // struct context *context = NULL;
    struct api_ipv4 *ipv4 = NULL;

    ipv4 = _api_ipv4_parse(param);
    if (ipv4 == NULL) {
        return -1;
    }

    return 0;
}

API_PATCH(ipv4, /v1/ipv4)
{
    return 0;
}

API_DELETE(ipv4, /v1/ipv4)
{
    return 0;
}

API_GET(ipv4, /v1/ipv4)
{
    int ret = 0;
    char *str = NULL;
    void *sess = NULL;
    sr_data_t *data = NULL;
    const char *errmsg = NULL;

    sess = api_param_get_session(param);
    ret = sr_get_data(sess, "/v1/ipv4", 0, 0, 0, &data);
    if (ret != 0 && ret != SR_ERR_NOT_FOUND) {
        errmsg = sr_strerror(ret);
        LOG_ERROR("Function(sr_get_data) error: %s", errmsg);
        api_param_set_error(param, API_ERRCODE_CONFIG_QUERY, errmsg);

        goto _quit;
    } else if (data == NULL || data->tree == NULL) {
        api_param_set_success(param, NULL);
        return 0;
    }

    str = _api_ipv4_get(param, data);
    if (str == NULL) {
        goto _quit;
    }

    sr_release_data(data);
    api_param_set_success(param, str);
    free(str);

    return 0;

_quit:
    sr_release_data(data);
    return -1;
}