/*****************************************************************************
 * filename: api_ipv4.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api.h"

#define API_IPV4_CONTAINER "/v1/ipv4"

static void *_ipv4_get(void *param, sr_data_t *data)
{
    LY_ERR err = 0;
    char *json = NULL;
    json_t *root = NULL;
    const char *errmsg = NULL;

    err = lyd_print_mem(&json, data->tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS);
    if (err != 0) {
        errmsg = ly_strerr(err);
        LOG_ERROR("Function(lyd_print_mem) failure: %s", errmsg);
        api_param_set_error(param, API_ERRCODE_IPv4_DB, errmsg);
        return NULL;
    }

    root = json_loads(json, 0, NULL);
    free(json);
    if (root == NULL) {
        LOG_ERROR("Function(json_loads) failure");
        api_param_set_error(param, API_ERRCODE_IPv4_FORMAT, "format error");
        return NULL;
    }

    return root;
}

API_POST(ipv4, /v1/ipv4)
{
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
    ret = sr_get_data(sess, API_IPV4_CONTAINER, 0, 0, 0, &data);
    if (ret != 0 && ret != SR_ERR_NOT_FOUND) {
        errmsg = sr_strerror(ret);
        LOG_ERROR("Function(sr_get_data) error: %s", errmsg);
        api_param_set_error(param, API_ERRCODE_IPv4_DB, errmsg);

        goto _quit;
    } else if (data == NULL || data->tree == NULL) {
        api_param_set_success(param, NULL);
        return 0;
    }

    str = _ipv4_get(param, data);
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