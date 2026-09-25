/*****************************************************************************
 * filename: config.c
 * function:
 * description:
 ****************************************************************************/

#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
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

    ret = sr_session_start(conn, SR_DS_RUNNING, (sr_session_ctx_t **)&config->session);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_session_start) failure: %s", sr_strerror(ret));
        sr_disconnect(conn);
        return -1;
    }

    ret = sr_copy_config(config->session, NULL, SR_DS_STARTUP, 0);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Function(sr_copy_config) failure: %s", sr_strerror(ret));
        goto _quit;
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