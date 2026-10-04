/*****************************************************************************
 * filename: config.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api.h"
#include "config.h"

static int _config_read_value(sr_data_t *data, const char *key, const char **value)
{
    int ret = 0;
    struct lyd_node *node = NULL;

    ret = lyd_find_path(data->tree, key, 0, &node);
    if (ret != LY_SUCCESS) {
        LOG_ERROR("Function(lyd_find_path) failure: %s", ly_strerr(ret));
        return -1;
    } else if (node == NULL) {
        LOG_INFO("Node(%s) not exists", key);
        return 0;
    }

    *value = lyd_get_value(node);
    if (*value == NULL) {
        LOG_ERROR("Function(lyd_get_value) failure");
        return -1;
    }

    return 0;
}

static int _config_read_listener(struct boot_config *config)
{
    int ret = 0;
    const char *value = NULL;
    sr_data_t *data = NULL;

    ret = sr_get_subtree(config->session, "/v1:boot/listener", 0, &data);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_get_subtree) failure: %s", sr_strerror(ret));
        return -1;
    }

    ret = _config_read_value(data, "ipv4/address", &value);
    if (ret != 0) {
        goto _quit;
    }

    strcpy(config->address, value);
    LOG_INFO("address: %s", config->address);

    ret = _config_read_value(data, "ipv4/http-port", &value);
    if (ret != 0) {
        goto _quit;
    }

    config->http_port = (value == NULL) ? 0 : atoi(value);
    LOG_INFO("http_port = %d", config->http_port);

    ret = _config_read_value(data, "ipv4/https-port", &value);
    if (ret != 0) {
        goto _quit;
    }

    config->https_port = (value == NULL) ? 0 : atoi(value);
    LOG_INFO("https_port = %d", config->https_port);

    sr_release_data(data);
    return 0;

_quit:
    if (data != NULL) {
        sr_release_data(data);
    }

    return -1;
}

static int _config_submodule_init(void **pp_json, void *sess, char *xpath)
{
    int ret;
    LY_ERR err = 0;
    char *str = NULL;
    void *json = NULL;
    sr_data_t *data = NULL;

    ret = sr_get_data(sess, xpath, 0, 0, 0, &data);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_get_data) failure: %s", sr_strerror(ret));
        return -1;
    }

    if (data == NULL || data->tree == NULL) {
        sr_release_data(data);
        return 0;
    }

    err = lyd_print_mem(&str, data->tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS);
    sr_release_data(data);
    if (err != LY_SUCCESS) {
        LOG_ERROR("Function(lyd_print_mem) failure: %s", ly_strerr(err));
        return -1;
    }

    json = json_loads(str, 0, NULL);
    if (json == NULL) {
        LOG_ERROR("String(%s) to json failure", str);
        return -1;
    }

    free(str);
    *pp_json = json;

    return 0;
}

static int _config_init(void *sess, const char *module_name, void *param)
{
    int ret = 0;
    int count = 0;
    void *json = NULL;
    char xpath[BUFSIZ] = "";
    sr_conn_ctx_t *conn = NULL;
    const struct ly_ctx *ly_ctx = NULL;
    const struct lys_module *module = NULL;
    const struct api_interface **interface = NULL;

    conn = sr_session_get_connection(sess);
    ly_ctx = sr_acquire_context(conn);

    module = ly_ctx_get_module_implemented(ly_ctx, module_name);
    if (module == NULL) {
        LOG_ERROR("Sysrepo (%s) not exist", module_name);
        return -1;
    }

    interface = api_get_all_post(&count);
    for (int i = 0; i < count; i++) {
        const struct lysp_submodule *submodule = NULL;
        const struct api_interface *one = interface[i];

        if (strcmp(one->container, "boot") == 0) {
            continue;
        }

        submodule = ly_ctx_get_submodule2(module, one->container, NULL);
        if (submodule == NULL) {
            continue;
        }

        snprintf(xpath, sizeof(xpath), "/%s:%s", module_name, one->container);
        ret = _config_submodule_init(&json, sess, xpath);
        if (ret != 0) {
            return -1;
        }

        api_param_set_input(param, json);

        ret = one->callback(param);
        json_decref(json);

        api_param_set_input(param, NULL);
        if (ret != 0) {
            return -1;
        }

        LOG_INFO("Load(%s) SUCCESS.", xpath);
    }

    return 0;
}

static int _config_update(sr_session_ctx_t *sess, uint32_t sub_id, const char *module_name,
                          const char *xpath, sr_event_t event, uint32_t operation_id, void *private_data)
{
    struct api_iface_param *iface_param = private_data;
    struct api_interface *iface = iface_param->iface;
    struct api_param *param = iface_param->param;

    switch (event) {
    case SR_EV_UPDATE:
        return iface->callback(param);
    case SR_EV_CHANGE:
        break;
    case SR_EV_DONE:
        return _config_init(sess, module_name, param);
        break;
    case SR_EV_ENABLED:
        break;
    default:
        break;
    }

    return 0;
}

static int _config_apply_changes(void *sess)
{
    int ret = 0;

    ret = sr_apply_changes(sess, 0);
    if (ret != 0) {
        LOG_ERROR("Function(sr_apply_changes) failure: %s", sr_strerror(ret));
        return API_ERRCODE_INTERNAL;
    }

    return API_ERRCODE_SUCCESS;
}

static int _config_parse_json(void *sess, const char *body, size_t len, struct lyd_node **tree)
{
    LY_ERR err = 0;
    void *conn = NULL;
    const struct ly_ctx *ly_ctx = NULL;

    conn = sr_session_get_connection(sess);
    ly_ctx = sr_acquire_context(conn);
    err = lyd_parse_data_mem(ly_ctx, body, LYD_JSON, LYD_PARSE_STRICT, LYD_VALIDATE_PRESENT, tree);
    if (err != LY_SUCCESS) {
        LOG_ERROR("Function(lyd_parse_data_mem) failure: %s", ly_strerr(err));
        return API_ERRCODE_INTERNAL;
    }

    sr_release_context(conn);
    return API_ERRCODE_SUCCESS;
}

static int _config_change(void *param, char *body, size_t len, const char *change)
{
    int ret = 0;
    void *root = NULL;
    void *sess = NULL;
    struct lyd_node *tree = NULL;

    if (param == NULL || body == NULL) {
        LOG_ERROR("Invalid parameter.");
        return API_ERRCODE_INTERNAL;
    }

    sess = api_param_get_session(param);
    ret = _config_parse_json(sess, body, len, &tree);
    if (ret != 0) {
        return ret;
    }

    ret = sr_edit_batch(sess, tree, change);
    lyd_free_all(tree);
    if (ret != 0) {
        LOG_ERROR("Function(sr_edit_batch) failure: %s", sr_strerror(ret));
        return API_ERRCODE_INTERNAL;
    }

    root = json_loadb(body, len, 0, NULL);
    if (root == NULL) {
        LOG_ERROR("Format(%*.s) error", len, body);
        return API_ERRCODE_FORMAT;
    }


    return _config_apply_changes(sess);
}

int config_post(void *param, char *body, size_t len)
{
    return _config_change(param, body, len, "merge");
}

int config_put(void *param, char *body, size_t len)
{
    return _config_change(param, body, len, "replace");
}

int config_patch(void *param, char *body, size_t len)
{
    return _config_change(param, body, len, "merge");
}

int config_delete(void *param, char *body, size_t len)
{
    return 0;
}

int config_get(void *param, char *body, size_t len)
{
    return 0;
}

void config_boot_free(struct boot_config *config)
{
    sr_conn_ctx_t *conn = NULL;

    if (config == NULL || config->session == NULL) {
        return;
    }

    conn = sr_session_get_connection(config->session);

    if (config->session != NULL) {
        sr_session_stop(config->session);
        config->session = NULL;
    }

    if (conn != NULL) {
        sr_disconnect(conn);
    }
}

int config_subscript(void *arg)
{
    int ret = 0;
    sr_session_ctx_t *sess = NULL;
    sr_subscription_ctx_t *subscript = NULL;
    struct api_iface_param *iface_param = arg;

    sess = api_param_get_session(iface_param->param);

    ret = sr_module_change_subscribe(sess, "v1", NULL, _config_update, iface_param, 0, SR_SUBSCR_UPDATE | SR_SUBSCR_ENABLED, &subscript);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_module_change_subscribt) failure: %s", sr_strerror(ret));
        return -1;
    }

    return 0;
}

int config_boot_load(struct boot_config *config)
{
    int ret = 0;
    sr_conn_ctx_t *conn = NULL;

    LOG_INFO("SYSREPO PATH: %s", sr_get_repo_path());
    LOG_INFO("SYSREPO SHM PATH: %s", sr_get_shm_path());

    ret = sr_connect(SR_CONN_DEFAULT, (sr_conn_ctx_t **)&conn);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_connect) failure: %s", sr_strerror(ret));
        return -1;
    }

    ret = sr_session_start(conn, SR_DS_STARTUP, (sr_session_ctx_t **)&config->session);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_session_start) failure: %s", sr_strerror(ret));
        sr_disconnect(conn);
        return -1;
    }

    ret = _config_read_listener(config);
    if (ret != 0) {
        goto _quit;
    }

    return 0;

_quit:
    config_boot_free(config);
    return -1;
}