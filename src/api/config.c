/*****************************************************************************
 * filename: config.c
 * function:
 * description:
 ****************************************************************************/

#define _GNU_SOURCE
#include <pthread.h>

#include <jansson.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api.h"
#include "config.h"

#define CONFIG_XPATH_LEN 128

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

static int _config_update_exec(sr_session_ctx_t *sess, uint32_t sub_id, const char *module_name,
                               const char *xpath, sr_event_t event, uint32_t operation_id, void *private_data)
{
    struct api_iface_param *iface_param = private_data;
    struct api_interface *iface = iface_param->iface;
    struct api_param *param = iface_param->param;

    LOG_INFO("event: %d\n", event);
    switch (event) {
    case SR_EV_UPDATE: // password
        break;
    case SR_EV_CHANGE:
        return iface->callback(param);
    case SR_EV_DONE:
        break;
    case SR_EV_ENABLED:
        pthread_setname_np(pthread_self(), "API_EXEC");
        return _config_init(sess, module_name, param);
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
        sr_discard_changes(sess);
        return API_STATUS_INTERNAL;
    }

    return API_STATUS_SUCCESS;
}

static int _config_parse_json(void *sess, const char *body, size_t len, struct lyd_node **tree)
{
    LY_ERR err = 0;
    void *conn = NULL;
    const struct ly_ctx *ly_ctx = NULL;

    conn = sr_session_get_connection(sess);
    ly_ctx = sr_acquire_context(conn);
    err = lyd_parse_data_mem(ly_ctx, body, LYD_JSON, LYD_PARSE_STRICT | LYD_PARSE_ONLY, 0, tree);
    if (err != LY_SUCCESS) {
        LOG_ERROR("Function(lyd_parse_data_mem) failure: %s", ly_strerr(err));
        return API_STATUS_INTERNAL;
    }

    if (*tree == NULL) {
        LOG_ERROR("Empty input");
        return API_STATUS_FORMAT;
    }

    return API_STATUS_SUCCESS;
}

static int _config_post_check_node(const struct lyd_node *node, const struct lyd_node *current)
{
    LY_ERR err = 0;
    char *path = NULL;
    struct lyd_node *match = NULL;
    const struct lyd_node *iter = NULL;

    LY_LIST_FOR(node, iter) {
        if (iter->schema->nodetype & LYS_CONTAINER) {
            const struct lysc_node_container *c = NULL;

            c = (const struct lysc_node_container *)iter->schema;
            if (!(c->flags & LYS_PRESENCE)) {
                if (!(c->flags & LYS_PRESENCE)) {
                    int ret = 0;

                    ret = _config_post_check_node(lyd_child(iter), current);
                    if (ret != 0) {
                        return ret;
                    }
                }

                continue;
            }
        }

        path = lyd_path(iter, LYD_PATH_STD, NULL, 0);
        if (path == NULL) {
            LOG_ERROR("Function(lyd_path) failure");
            return API_STATUS_INTERNAL;
        }

        match = NULL;
        if (current != NULL) {
            err = lyd_find_path(current, path, 0, &match);
            if (err == LY_SUCCESS && match != NULL) {
                LOG_ERROR("POST target already exists: %s", path);
                free(path);
                return API_STATUS_EXIST;
            }

            if ((err != LY_SUCCESS) && (err != LY_ENOTFOUND) && (err != LY_EINCOMPLETE)) {
                LOG_ERROR("Function(lyd_find_path) failure: path=%s, error=%s", path, ly_strerr(err));
                free(path);
                return API_STATUS_INTERNAL;
            }
        }

        free(path);
        path = NULL;
    }

    return 0;
}

static int _config_patch_check_node(const struct lyd_node *node, const struct lyd_node *current)
{
    LY_ERR err = 0;
    char *path = NULL;
    struct lyd_node *match = NULL;
    const struct lyd_node *iter = NULL;

    LY_LIST_FOR(node, iter) {
        if (iter->schema->nodetype & LYS_CONTAINER) {
            const struct lysc_node_container *c = NULL;

            c = (const struct lysc_node_container *)iter->schema;
            if (!(c->flags & LYS_PRESENCE)) {
                int ret = 0;

                ret = _config_patch_check_node(lyd_child(iter), current);
                if (ret != 0) {
                    return ret;
                }

                continue;
            }
        }

        path = lyd_path(iter, LYD_PATH_STD, NULL, 0);
        if (path == NULL) {
            LOG_ERROR("Function(lyd_path) failure");
            return API_STATUS_INTERNAL;
        }

        match = NULL;
        if (current != NULL) {
            err = lyd_find_path(current, path, 0, &match);
            if (err != LY_SUCCESS || match == NULL) {
                LOG_ERROR("Function(lyd_find_path) failure: path=%s, error=%s", path, ly_strerr(err));
                free(path);
                return API_STATUS_NOT_FOUND;
            }
        }

        free(path);
        path = NULL;
    }

    return 0;
}

static int _config_check(void *sess, struct lyd_node *tree, enum API_HTTP_METHOD method)
{
    int ret = 0;
    sr_data_t *data = NULL;
    char xpath[CONFIG_XPATH_LEN] = "";

    if (lyd_path(tree, LYD_PATH_STD, xpath, sizeof(xpath)) == NULL) {
        LOG_ERROR("Function(lyd_path) failure.");
        return API_STATUS_INTERNAL;
    }

    ret = sr_get_data(sess, xpath, 0, 0, 0, &data);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_get_data) failure: %s", sr_strerror(ret));
        return API_STATUS_INTERNAL;
    }

    if (method == API_HTTP_POST) {
        ret = _config_post_check_node(tree, data != NULL ? data->tree : NULL);
    } else {
        ret = _config_patch_check_node(tree, data != NULL ? data->tree : NULL);
    }

    if (ret != 0) {
        if (data != NULL) {
            sr_release_data(data);
            data = NULL;
        }

        return ret;
    }

    if (data != NULL) {
        sr_release_data(data);
        data = NULL;
    }

    return ret;
}

static int _config_update(void *param, char *body, size_t len, enum API_HTTP_METHOD method)
{
    int ret = 0;
    void *sess = NULL;
    void *root = NULL;
    struct lyd_node *tree = NULL;

    if (param == NULL || body == NULL || len == 0) {
        LOG_ERROR("Invalid parameter.");
        return API_STATUS_INTERNAL;
    }

    sess = api_param_get_session(param);
    ret = _config_parse_json(sess, body, len, &tree);
    if (ret != 0) {
        return ret;
    }

    ret = _config_check(sess, tree, method);
    if (ret != 0) {
        lyd_free_all(tree);
        return ret;
    }

    ret = sr_edit_batch(sess, tree, "merge");
    lyd_free_all(tree);
    if (ret != 0) {
        LOG_ERROR("Function(sr_edit_batch) failure: %s", sr_strerror(ret));
        return API_STATUS_INTERNAL;
    }

    root = json_loadb(body, len, 0, NULL);
    if (root == NULL) {
        LOG_ERROR("Format(%.*s) error", len, body);
        sr_discard_changes(sess);
        return API_STATUS_FORMAT;
    }

    api_param_set_input(param, root);
    return _config_apply_changes(sess);
}

static int __config_delete(void *sess, const struct lyd_node *node)
{
    int ret = 0;
    char *xpath = NULL;

    xpath = lyd_path(node, LYD_PATH_STD, NULL, 0);
    if (xpath == NULL) {
        LOG_ERROR("Function(lyd_path) failure");
        return API_STATUS_FORMAT;
    }

    ret = sr_delete_item(sess, xpath, SR_EDIT_STRICT);
    if (ret != 0) {
        LOG_ERROR("Function(sr_delete_item) failure: xpath=%s, error=%s", xpath, sr_strerror(ret));
        ret = API_STATUS_INTERNAL;
    }

    free(xpath);
    return ret;
}

static int _config_delete(void *sess, struct lyd_node *tree)
{
    int ret = 0;
    const struct lyd_node *node = NULL;
    const struct lysc_node_container *container = NULL;

    LY_LIST_FOR(tree, node) {
        switch (node->schema->nodetype) {
        case LYS_LIST:
            ret = __config_delete(sess, node);
            if (ret != 0) {
                return ret;
            }
            break;

        case LYS_CONTAINER:
            container = (const struct lysc_node_container *)node->schema;

            if (container->flags & LYS_PRESENCE) {
                ret = __config_delete(sess, node);
                if (ret != 0) {
                    return ret;
                }

                continue;
            }

            if (lyd_child(node) != NULL) {
                ret = _config_delete(sess, lyd_child(node));
                if (ret != 0) {
                    return ret;
                }
            }
            break;

        case LYS_LEAFLIST:
            ret = __config_delete(sess, node);
            if (ret != 0) {
                return ret;
            }
            break;

        case LYS_LEAF:
            if ((node->schema->flags & LYS_KEY) != 0) {
                continue;
            }

            ret = __config_delete(sess, node);
            if (ret != 0) {
                return ret;
            }
            break;

        case LYS_ANYXML:
        case LYS_ANYDATA:
            ret = __config_delete(sess, node);
            if (ret != API_STATUS_SUCCESS) {
                return ret;
            }
            break;

        default:
            LOG_ERROR("Unsupported node type: name=%s, nodetype=0x%x", node->schema->name, node->schema->nodetype);
            return API_STATUS_FORMAT;
        }
    }

    return 0;
}

int config_post(void *param, char *body, size_t len)
{
    return _config_update(param, body, len, API_HTTP_POST);
}

int config_patch(void *param, char *body, size_t len)
{
    return _config_update(param, body, len, API_HTTP_PATCH);
}

int config_delete(void *param, char *body, size_t len)
{
    int ret = 0;
    void *sess = NULL;
    void *root = NULL;
    struct lyd_node *tree = NULL;

    if (param == NULL || body == NULL || len == 0) {
        LOG_ERROR("Invalid parameter");
        return API_STATUS_INTERNAL;
    }

    sess = api_param_get_session(param);
    ret = _config_parse_json(sess, body, len, &tree);
    if (ret != 0) {
        return ret;
    }

    ret = _config_delete(sess, tree);
    lyd_free_all(tree);
    if (ret != 0) {
        sr_discard_changes(sess);
        return ret;
    }

    root = json_loadb(body, len, 0, NULL);
    if (root == NULL) {
        LOG_ERROR("Format(%.*s) error", (int)len, body);
        sr_discard_changes(sess);
        return API_STATUS_FORMAT;
    }

    api_param_set_input(param, root);
    return _config_apply_changes(sess);
}

int config_get(void *param, char *body, size_t len)
{
    int ret = 0;
    void *root = NULL;
    void *sess = NULL;
    sr_val_t *val = NULL;
    const char *xpath = "/v1:query/counter";

    if (param == NULL) {
        LOG_ERROR("Invalid parameter.");
        return API_STATUS_INTERNAL;
    }

    sess = api_param_get_session(param);
    ret = sr_get_item(sess, xpath, 0, &val);
    if (ret != 0) {
        LOG_ERROR("Get xpath failure: %s", sr_strerror(ret));
        return API_STATUS_INTERNAL;
    }

    val->data.uint64_val += 1;
    ret = sr_set_item(sess, xpath, val, SR_EDIT_DEFAULT);
    sr_free_val(val);
    if (ret != 0) {
        LOG_ERROR("Set val failure: %s", sr_strerror(ret));
        return API_STATUS_INTERNAL;
    }

    if (body != NULL && len != 0) {
        root = json_loadb(body, len, 0, NULL);
        if (root == NULL) {
            LOG_ERROR("Format(%.*s) error", (int)len, body);
            sr_discard_changes(sess);
            return API_STATUS_FORMAT;
        }

        api_param_set_input(param, root);
    }

    return _config_apply_changes(sess);
}

void config_param_clean(void *param)
{
    void *in = NULL;

    in = api_param_get_input(param);
    json_decref(in);
    api_param_set_input(param, NULL);
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

    ret = sr_module_change_subscribe(sess, "v1", NULL, _config_update_exec, iface_param, 0, SR_SUBSCR_UPDATE | SR_SUBSCR_ENABLED, &subscript);
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