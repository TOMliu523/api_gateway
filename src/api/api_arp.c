/*****************************************************************************
 * filename: api_arp.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api.h"

#define API_ARP_CONTAINER "/v1:arp"

static char *_arp_get(void *param, sr_data_t *data)
{
    LY_ERR err = 0;
    char *json = NULL;
    json_t *root = NULL;
    const char *errmsg = NULL;

    if (data == NULL || data->tree == NULL) {
        api_param_set_success(param, "");
        return NULL;
    }

    err = lyd_print_mem(&json, data->tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS);
    if (err != 0) {
        errmsg = ly_strerr(err);
        LOG_ERROR("Function(lyd_print_mem) failure: %s", errmsg);
        api_param_set_error(param, API_ERRCODE_ARP_DB, errmsg);
        return NULL;
    }

    root = json_loads(json, 0, NULL);
    free(json);
    if (root == NULL) {
        LOG_ERROR("Function(json_loads) failure");
        api_param_set_error(param, API_ERRCODE_ARP_FORMAT, "format error");
        return NULL;
    }

    json = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (json == NULL) {
        LOG_ERROR("Function(json_dumps) failure");
        api_param_set_error(param, API_ERRCODE_ARP_FORMAT, "json format error");
        return NULL;
    }

    return json;
}

API_POST(arp, /v1/arp)
{
    LOG_INFO("POST: /v1/arp");

    return 0;
}

API_PATCH(arp, /v1/arp)
{
    LOG_INFO("PATHC: /v1/arp");
    return 0;
}

API_DELETE(arp, /v1/arp)
{
    LOG_INFO("DELETE: /v1/arp");
    return 0;
}

API_GET(arp, /v1/arp)
{
    int ret = 0;
    char *str = NULL;
    void *sess = NULL;
    sr_data_t *data = NULL;
    const char *errmsg = NULL;

    sess = api_param_get_session(param);
    ret = sr_get_data(sess, API_ARP_CONTAINER, 0, 0, 0, &data);
    if (ret != 0) {
        errmsg = sr_strerror(ret);
        LOG_ERROR("Function(sr_get_data) error: %s", errmsg);
        api_param_set_error(param, API_ERRCODE_ARP_DB, errmsg);
        goto _quit;
    }

    str = _arp_get(param, data);
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