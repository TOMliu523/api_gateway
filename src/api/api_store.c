/************************************************
 * filename: api_store.h
 * function:
 * description:
 ***********************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/epoll.h>

#include <sysrepo.h>
#include <jansson.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api_inner.h"

#define API_TIMEOUT (30 * 1000)
#define API_YANG_MODULE "v1"
#define API_YANG_PATH "conf/yang/"
#define API_YANG_V1 API_YANG_PATH API_YANG_MODULE".yang"
#define API_YANG_COMMON API_YANG_PATH "common/"

#define API_DEBUG_DIR "/root/api_gateway/"

struct db {
    sr_conn_ctx_t *conn;
    sr_session_ctx_t *session;
    sr_subscription_ctx_t *subscript;
    struct private_data {
        api_action_fn_t action;
        const char *url;
        void *input;
        void *output;
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
    json_t *req = NULL;
    struct private_data *data = private_data;

    switch (event) {
    case SR_EV_CHANGE:
        req = data->action(data->url, data->input, session);
        if (req == NULL) {
            return -1;
        }
        data->output = req;
        break;
    case SR_EV_DONE:
        break;
    default:
        break;
    }

    return 0;
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

static void *_api_store_cb(void *arg)
{
    int ret = 0;
    int epoll_fd = -1;
    int event_pipe_fd = -1;
    struct db *db = arg;
    struct epoll_event ev = {0};
    struct epoll_event events[1];

    pthread_setname_np(pthread_self(), "API_LOADING");
    sr_get_event_pipe(db->subscript, &event_pipe_fd);

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERROR("epoll_create1 failure: %s", strerror(epoll_fd));
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = event_pipe_fd;

    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_pipe_fd, &ev);
    if (ret < 0) {
        LOG_ERROR("epoll_ctl failure: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    for (;;) {
        int n = epoll_wait(epoll_fd, events, 1, 1000);
        if (n > 0 && (events[0].events & EPOLLIN) != 0) {
            sr_subscription_process_events(db->subscript, db->session, NULL);
        }
    }

    close(epoll_fd);
    pthread_exit(NULL);
}

static void _api_store_set(struct private_data *data, const struct api_method_node *api, void *rep)
{
    data->action = api->action;
    data->input = rep;
    data->url = api->url;
    data->output = NULL;
}

static void *_api_store_get(struct private_data *data)
{
    void *output = data->output;
    data->output = NULL;
    return output;
}

int api_store_query(const struct api_method_node *api, const char *buf, size_t len, void **req)
{
    int ret = 0;
    json_t *rep = NULL;
    sr_val_t *val = NULL;
    struct db *db = &s_db;
    json_error_t error = {0};
    const char *format = "/v1:query/counter";

    ret = sr_get_item(db->session, format, 0, &val);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_get_item failure: %s", sr_strerror(ret));
        return -1;
    }

    val->data.uint64_val += 1;
    ret = sr_set_item(db->session, format, val, SR_EDIT_DEFAULT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_set_item failure: %s", sr_strerror(ret));
        return -1;
    }

    if (len != 0) {
        rep = json_loadb(buf, len, 0, &error);
        if (rep == NULL) {
            LOG_ERROR("json_loadb failure.");
            return -1;
        }
    }

    _api_store_set(&db->data, api, rep);

    ret = sr_apply_changes(db->session, API_TIMEOUT);
    if (ret != 0) {
        LOG_ERROR("sr_apply_change failure: %s", sr_strerror(ret));
        return -1;
    }

    *req = _api_store_get(&db->data);
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

int api_store_init(void)
{
    int ret = 0;
    pthread_t thid = {0};
    struct db *db = &s_db;
    LY_ERR err = LY_SUCCESS;
    struct lys_module *module = NULL;
    const char *schema_paths[] = {
        API_DEBUG_DIR API_YANG_V1,
        NULL,
    };
    const char *search_dir = API_DEBUG_DIR API_YANG_COMMON \
                             ":" API_DEBUG_DIR API_YANG_PATH;

    sr_log_stderr(SR_LL_DBG);
    sr_log_set_cb(_api_store_db_log);
    ly_log_level(LY_LLDBG);
    ly_set_log_clb(_api_store_yang_log);

    ret = sr_connect(SR_CONN_DEFAULT, &db->conn);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session connect error: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = sr_session_start(db->conn, SR_DS_RUNNING, &db->session);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session start error: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = sr_install_modules(db->conn, schema_paths, search_dir, NULL);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session install error: %s", sr_strerror(ret));
        goto _quit;
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
        LOG_ERROR("sr_module_change_subscribe failure: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = pthread_create(&thid, NULL, _api_store_cb, db);
    if (ret != 0) {
        LOG_ERROR("pthread_create failure: %s", strerror(ret));
        goto _quit;
    }

    LOG_DEBUG("module load success.");

    return 0;

_quit:
    api_store_fini();
    return -1;
}

void api_store_fini(void)
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