/************************************************
 * filename: api_store.h
 * function:
 * description:
 ***********************************************/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api_inner.h"

#define API_YANG_MODULE "api"
#define API_YANG_PATH "conf/yang/"
#define API_YANG_STARTUP API_YANG_PATH API_YANG_MODULE".yang"
#define API_YANG_COMMON API_YANG_PATH "common/"

struct db {
    sr_conn_ctx_t *conn;
    sr_session_ctx_t *session;
    sr_subscription_ctx_t *subscript;
    struct private_data {
        api_action_fn_t action;
        const char *url;
        void *json;
        void *session;
    } data;
};

static struct db s_db;

static int _api_store_update_cb(sr_session_ctx_t *session,
                                uint32_t sub_id,
                                const char *module_name,
                                const char *xpath,
                                sr_event_t event,
                                uint32_t operation_id,
                                void *private_data)
{
    return 0;
}

static PROC_FINI void _api_store_fini(void)
{
    struct db *db = &s_db;

    if (db->subscript != NULL) {
        sr_unsubscribe(db->subscript);
        db->subscript = NULL;
    }

    if (db->session != NULL) {
        sr_session_stop(db->session);
        db->session = NULL;
    }

    if (db->conn != NULL) {
        sr_disconnect(db->conn);
        db->conn = NULL;
    }
}

static void _api_store_db_log(sr_log_level_t level, const char *message)
{
    switch (level) {
    case SR_LL_ERR: LOG_ERROR("%s", message); break;
    case SR_LL_WRN: LOG_WARN("%s", message); break;
    case SR_LL_INF: LOG_INFO("%s", message); break;
    case SR_LL_DBG: LOG_DEBUG("%s", message); break;
    }
}

static void _api_store_yang_log(LY_LOG_LEVEL level,
                                const char *msg,
                                const char *data_path,
                                const char *schema_path,
                                uint64_t line)
{
    switch (level) {
    case LY_LLERR: LOG_ERROR("%s", msg); break;
    case LY_LLWRN: LOG_WARN("%s", msg); break;
    case LY_LLVRB: LOG_INFO("%s", msg); break;
    case LY_LLDBG: LOG_DEBUG("%s", msg); break;
    }
}

static PROC_INIT void _api_store_init(void)
{
    int ret = 0;
    struct db *db = &s_db;
    LY_ERR err = LY_SUCCESS;
    struct lys_module *module = NULL;
    const char *schema_paths[] = {
        API_YANG_STARTUP,
        NULL,
    };
    const char *search_dir = API_YANG_COMMON;

    sr_log_stderr(SR_LL_DBG);
    sr_log_set_cb(_api_store_db_log);
    ly_log_level(LY_LLDBG);
    ly_set_log_clb(_api_store_yang_log);

    ret = sr_connect(SR_CONN_DEFAULT, &db->conn);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session connect error: %s\n", sr_strerror(ret));
        exit(EXIT_FAILURE);
    }

    ret = sr_session_start(db->conn, SR_DS_RUNNING, &db->session);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session start error: %s\n", sr_strerror(ret));
        exit(EXIT_FAILURE);
    }

    ret = sr_install_modules(db->conn, schema_paths, search_dir, NULL);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session install error: %s\n", sr_strerror(ret));
        exit(EXIT_FAILURE);
    }

    ret = sr_module_change_subscribe(db->session,
                                     API_YANG_MODULE,
                                     NULL,
                                    _api_store_update_cb,
                                    &db->data,
                                    0,
                                    SR_SUBSCR_NO_THREAD,
                                    &db->subscript);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_module_change_subscribe failure: %s\n", sr_strerror(ret));
        exit(EXIT_FAILURE);
    }
}

int api_store_query(const struct api_method_node *api, const char *buf, size_t len, void **req)
{
    return 0;
}

int api_store_update(const struct api_method_node *api, const char *buf, size_t len, void **req)
{
    return 0;
}

int api_store_create(const struct api_method_node *api, const char *buf, size_t len, void **req)
{
    return 0;
}

int api_store_delete(const struct api_method_node *api, const char *buf, size_t len, void **req)
{
    return 0;
}