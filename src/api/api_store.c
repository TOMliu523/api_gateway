/************************************************
 * filename: api_store.h
 * function:
 * description:
 ***********************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/epoll.h>

#include <sysrepo.h>
#include <jansson.h>
#include <libyang/libyang.h>

#include "log.h"
#include "hash.h"
#include "api_inner.h"

#define API_CONTAINER_NUMS 1024
#define API_TIMEOUT (30 * 1000)
#define API_YANG_MODULE "v1"
#define API_YANG_PATH "conf/yang/"
#define API_YANG_V1 API_YANG_PATH API_YANG_MODULE".yang"
#define API_YANG_COMMON API_YANG_PATH "common/"

#define API_DEBUG_DIR "/root/api_gateway/"

struct api_startup {
    bool head_init;
    struct list_head head[API_METHOD_TABLE];
    const char *container[API_CONTAINER_NUMS];
    int nums;
};

 struct api_db {
    sr_conn_ctx_t *conn;
    sr_session_ctx_t *sess;
    sr_subscription_ctx_t *subscript;
    struct private_data {
        api_action_fn_t action;
        const char *url;
        void *input;
        void *output;
    } data;
};

static struct api_db s_api_db;
static struct api_startup s_api_startup;
static __thread char s_buffer[BUFSIZ * 4];

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

static int _api_store_update_cb(sr_session_ctx_t *session,
                                uint32_t sub_id,
                                const char *module_name,
                                const char *xpath,
                                sr_event_t event,
                                uint32_t operation_id,
                                void *private_data)
{
    json_t *req = NULL;
     struct api_db *db = NULL;
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

static void _api_method_init(void)
{
    struct api_startup *startup = &s_api_startup;

    if (startup->head_init) {
        return;
    }

    for (int i = 0; i < API_METHOD_TABLE; i++) {
        INIT_LIST_HEAD(&startup->head[i]);
    }

    startup->head_init = true;
}

static const struct api_method_node *_api_method_get(const char *container)
{
    int ret = 0;
    uint32_t hash = 0;
    struct list_head *head = NULL;
    struct api_method_node *curr = NULL;
    struct api_method_node *next = NULL;
    struct api_startup *startup = &s_api_startup;

    hash_32(container, strlen(container), 0, &hash);
    head = &startup->head[API_HASH_TABLE_INDEX(hash)];

    list_for_each_entry_safe(curr, next, head, node) {
        if (curr->hash < hash) {
            continue;
        } else if (curr->hash == hash) {
            ret = strcmp(curr->container, container);
            if (ret < 0) {
                continue;
            } else if (ret == 0) {
                return curr;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return NULL;
}

static int _api_set_container(const char *container, const char *url, api_action_fn_t action)
{
    int ret = 0;
    uint32_t hash = 0;
    uint32_t url_len = 0;
    struct list_head *prev = NULL;
    struct list_head *head = NULL;
    struct api_method_node *one = NULL;
    struct api_method_node *curr = NULL;
    struct api_method_node *next = NULL;
    struct api_startup *startup = &s_api_startup;

    _api_method_init();

    url_len = strlen(url);
    hash_32(container, strlen(container), 0, &hash);
    head = &startup->head[API_HASH_TABLE_INDEX(hash)];

    prev = head;
    list_for_each_entry_safe (curr, next, head, node) {
        if (curr->hash < hash) {
            prev = &curr->node;
            continue;
        } else if (curr->hash == hash) {
            ret = strcmp(curr->container, container);
            if (ret < 0) {
                prev = &curr->node;
                continue;
            } else if (ret == 0) {
                LOG_WARN("register container(%s) exists", url);
                return 0;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    one = malloc(sizeof(*one));
    if (one == NULL) {
        LOG_ERROR("OOM.");
        exit(EXIT_FAILURE);
    }

    INIT_LIST_HEAD(&one->node);
    one->container = container;
    one->url = url;
    one->len = url_len;
    one->hash = hash;
    one->action = action;

    list_add(&one->node, prev);

    RUNTIME_ASSERT(startup->nums < API_CONTAINER_NUMS);
    startup->container[startup->nums++] = container;

    return 0;
}

void api_container_register(const char *url, const char *container, api_action_fn_t action)
{
    _api_set_container(container, url, action);
}

static int _api_store_load(sr_session_ctx_t *sess, const char *module_name)
{
    int ret = 0;
    LY_ERR err = 0;
    json_t *code = 0;
    void *json = NULL;
    void *ret_json = NULL;
    char *json_str = NULL;
    json_error_t error = {0};
    sr_data_t *subtree = NULL;
    const char *container = NULL;
    const struct api_method_node *api = NULL;
    const struct api_startup *startup = &s_api_startup;

    for (int i = 0; i < startup->nums; i++) {
        container = startup->container[i];
        snprintf(s_buffer, sizeof(s_buffer), "/%s:%s", module_name, container);
        ret = sr_get_subtree(sess, s_buffer, 0, &subtree);
        if (ret != 0) {
            LOG_ERROR("sr_get_subtree failure: %s", sr_strerror(ret));
            return -1;
        }

        api = _api_method_get(container);
        if (api == NULL) {
            LOG_ERROR("_api_method_get get container(%s) failure.", container);
            goto _quit;
        }

        err = lyd_print_mem(&json_str, subtree->tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS);
        if (err != LY_SUCCESS) {
            LOG_ERROR("lyd_print_mem failure: %s", ly_strerr(err));
            goto _quit;
        }

        json = json_loads(json_str, 0, &error);
        if (json == NULL) {
            LOG_ERROR("line: %d, column: %d, position: %d, source: %s, text: %s",
                      error.line, error.column, error.position, error.source, error.text);
            goto _quit;
        }

        ret_json = api->action(api->url, json, sess);
        if (ret_json == NULL) {
            goto _quit;
        }

        code = json_object_get(ret_json, "code");
        if (code == NULL) {
            LOG_ERROR("json not exist code.");
            goto _quit;
        }

        if (code->type != JSON_INTEGER || json_string_value(code) != 0) {
            LOG_ERROR("code failure.");
            goto _quit;
        }

        free(json_str); json_str = NULL;
        json_decref(json); json = NULL;
        sr_release_data(subtree); subtree = NULL;
    }

    return 0;

_quit:
    if (json_str != NULL) {
        free(json_str);
    }
    if (json != NULL) {
        json_decref(json);
    }
    if (subtree != NULL) {
        sr_release_data(subtree);
    }

    return -1;
}

static void *_api_store_cb(void *arg)
{
    int ret = 0;
    int epoll_fd = -1;
    int event_pipe_fd = -1;
     struct api_db *db = arg;
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
            sr_subscription_process_events(db->subscript, db->sess, NULL);
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
    void *output = NULL;

    if (data->output != NULL) {
        output = data->output;
        data->output = NULL;
        return output;
    } else {
        return api_success(NULL);
    }
}

static int _api_store_apply( struct api_db *db, const struct api_method_node *api, void *input, void **output)
{
    int ret = 0;

    _api_store_set(&db->data, api, input);
    ret = sr_apply_changes(db->sess, API_TIMEOUT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_apply_changes failure: %s", sr_strerror(ret));
        return -1;
    }

    *output = _api_store_get(&db->data);
    return 0;
}

static int _api_store_to_json(void **obj, const char *buf, size_t len)
{
    json_t *json = NULL;
    json_error_t error = {0};

    if (len != 0) {
        *obj = json_loadb(buf, len, 0, &error);
        if (*obj == NULL) {
            LOG_ERROR("line: %d, column: %d, position: %d, source: %s, text: %s",
                      error.line, error.column, error.position, error.source, error.text);
            return -1;
        }
    }

    return 0;
}

static int _api_store_delete_one( struct api_db *db, const char *key, void *value)
{
    int n = 0;
    int ret = 0;
    void *subvalue = NULL;
    const char *subkey = NULL;

    n = snprintf(s_buffer, sizeof(s_buffer), "%s", key);
    json_object_foreach(value, subkey, subvalue) {
        const char *v_str = json_string_value(subvalue);
        n += snprintf(s_buffer + n, sizeof(s_buffer) - n, "[%s='%s']", subkey, v_str);
    }

    ret = sr_delete_item(db->sess, s_buffer, SR_EDIT_DEFAULT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
        return -1;
    }

    return 0;
}

static int _api_store_add(sr_session_ctx_t *session, const struct lyd_node *node)
{
    int ret = 0;
    char tmp[256] = {0};
    const char *xpath = NULL;
    const char *value = NULL;

    for (; node != NULL; node = node->next) {
        if ((node->schema->nodetype & LYD_NODE_TERM) == 0) {
            ret = _api_store_add(session, lyd_child(node));
            if (ret != 0) {
                return ret;
            }
        } else {
            if (lysc_is_key(node->schema)) {
                continue;
            }

            xpath = lyd_path(node, LYD_PATH_STD, tmp, sizeof(tmp));
            if (xpath == NULL) {
                LOG_ERROR("lyd_path failure: %s", node->schema->name);
                return -1;
            }

            value = lyd_get_value(node);

            LOG_DEBUG("sr_set_item_str(session, %s, %s, NULL, SR_EDIT_DEFAULT)", xpath, value);
            ret = sr_set_item_str(session, xpath, value, NULL, SR_EDIT_DEFAULT);
            if (ret != 0) {
                LOG_ERROR("sr_get_item_str failure: xpath(%s), value(%s)", xpath, value);
                return -1;
            }
        }
    }

    return 0;
}

static int _api_store_create(struct lyd_node *node, bool create)
{
    int n = 0;
    int ret = 0;
     struct api_db *db = &s_api_db;
    sr_val_t *value = NULL;
    const struct lyd_node *next = NULL;
    const struct lyd_node *child = NULL;

    child = lyd_child(node);
    if (child != NULL && child->schema->nodetype == LYS_LIST) {
        for (; child != NULL; child = child->next) {
            n = 0;
            n = snprintf(s_buffer, sizeof(s_buffer), "/%s:%s/%s",
                        lyd_node_module(node)->name, node->schema->name, child->schema->name);
            next = lyd_child(child);
            while (next != NULL) {
                if (lysc_is_key(next->schema)) {
                    n += snprintf(s_buffer + n, sizeof(s_buffer) - n, "[%s='%s']", next->schema->name, lyd_get_value(next));
                }
                next = next->next;
            }

            if (create) {
                ret = sr_get_item(db->sess, s_buffer, 0, &value);
                sr_free_val(value);
                if (ret != SR_ERR_NOT_FOUND) {
                    LOG_ERROR("sr_get_item failure: %s exists.", s_buffer);
                    return -1;
                }
            } else {
                ret = sr_get_item(db->sess, s_buffer, 0, &value);
                sr_free_val(value);
                if (ret == SR_ERR_NOT_FOUND) {
                    LOG_ERROR("sr_get_item failure: %s.", sr_strerror(errno));
                    return -1;
                }

                ret = sr_delete_item(db->sess, s_buffer, SR_EDIT_DEFAULT);
                if (ret != SR_ERR_OK) {
                    LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
                    return -1;
                }
            }
        }

        ret = _api_store_add(db->sess, node);
        if (ret != 0) {
            return -1;
        }
    } else {
        ret = sr_edit_batch(db->sess, node, "replace");
        if (ret != SR_ERR_OK) {
            LOG_ERROR("sr_edit_batch failure: %s", sr_strerror(ret));
            return -1;
        }
    }

    return 0;
}

int api_store_query(const struct api_method_node *api, const char *buf, size_t len, void **output)
{
    int ret = 0;
    void *input = NULL;
    sr_val_t *val = NULL;
     struct api_db *db = &s_api_db;
    const char *format = "/v1:query/counter";

    RUNTIME_ASSERT(api != NULL && output != NULL);

    ret = sr_get_item(db->sess, format, 0, &val);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_get_item failure: %s", sr_strerror(ret));
        return -1;
    }

    val->data.uint64_val += 1;
    ret = sr_set_item(db->sess, format, val, SR_EDIT_DEFAULT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_set_item failure: %s", sr_strerror(ret));
        return -1;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        return -1;
    }

    return _api_store_apply(db, api, input, output);
}

int api_store_update(const struct api_method_node *api, const char *buf, size_t len, void **output)
{
    int ret = 0;
    void *input = NULL;
     struct api_db *db = &s_api_db;
    LY_ERR err = LY_SUCCESS;
    struct lyd_node *node = NULL;
    const struct ly_ctx *ctx = NULL;

    RUNTIME_ASSERT(api != NULL && output != NULL);

    ctx = sr_session_acquire_context(db->sess);
    err = lyd_parse_data_mem(ctx, buf, LYD_JSON, LYD_PARSE_ONLY, LYD_VALIDATE_MULTI_ERROR, &node);
    if (err != LY_SUCCESS) {
        LOG_ERROR("lyd_parse_data_mem failure: %s", ly_strerr(err));
        goto _quit;
    }

    ret = _api_store_create(node, false);
    if (ret != 0) {
        goto _quit;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        goto _quit;
    }

    _api_store_set(&db->data, api, input);

    ret = sr_apply_changes(db->sess, API_TIMEOUT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_apply_changes %s", sr_strerror(ret));
        goto _quit;
    }

    *output = _api_store_get(&db->data);

_quit:
    if (node != NULL) {
        lyd_free_tree(node);
    }
    if (input != NULL) {
        json_decref(input);
    }
    sr_discard_changes(db->sess);
    sr_session_release_context(db->sess);
    return (ret == 0) ? 0 : -1;
}

int api_store_create(const struct api_method_node *api, const char *buf, size_t len, void **output)
{
    int ret = 0;
    void *input = NULL;
     struct api_db *db = &s_api_db;
    LY_ERR err = LY_SUCCESS;
    struct lyd_node *node = NULL;
    const struct ly_ctx *ctx = NULL;

    RUNTIME_ASSERT(api != NULL && output != NULL);

    ctx = sr_session_acquire_context(db->sess);
    err = lyd_parse_data_mem(ctx, buf, LYD_JSON, LYD_PARSE_ONLY, LYD_VALIDATE_MULTI_ERROR, &node);
    if (err != LY_SUCCESS) {
        LOG_ERROR("lyd_parse_data_mem failure: %s", ly_strerr(err));
        return -1;
    }

    ret = _api_store_create(node, true);
    if (ret != 0) {
        goto _quit;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        goto _quit;
    }

    ret = _api_store_apply(db, api, input, output);

_quit:
    if (node != NULL) {
        lyd_free_tree(node);
    }
    if (input != NULL) {
        json_decref(input);
    }
    sr_discard_changes(db->sess);
    sr_session_release_context(db->sess);
    return (ret == 0) ? 0 : -1;
}

int api_store_delete(const struct api_method_node *api, const char *buf, size_t len, void **output)
{
    int ret = 0;
    int index = 0;
    void *one = NULL;
    void *iter = NULL;
    void *input = NULL;
    json_t *value = NULL;
     struct api_db *db = &s_api_db;
    const char *key = NULL;
    LY_ERR err = LY_SUCCESS;

    RUNTIME_ASSERT(api != NULL && output != NULL);

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        goto _quit;
    }

    iter = json_object_iter(input);
    if (iter == NULL) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    key = json_object_iter_key(iter);
    value = json_object_iter_value(iter);
    if (json_is_array(value)) {
        int nums = json_array_size(value);
        if (nums == 0) {
            ret = sr_delete_item(db->sess, key, SR_EDIT_DEFAULT);
            if (ret != SR_ERR_OK) {
                LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
                goto _quit;
            }
        } else {
            json_array_foreach(value, index, one) {
                ret = _api_store_delete_one(db, key, one);
                if (ret != 0) {
                    goto _quit;
                }
            }
        }
    } else {
        if (json_is_null(value)) {
            ret = sr_delete_item(db->sess, key, SR_EDIT_DEFAULT);
            if (ret != SR_ERR_OK) {
                LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
                goto _quit;
            }
        } else {
            ret = _api_store_delete_one(db, key, one);
            if (ret != 0) {
                goto _quit;
            }
        }
    }

    ret = _api_store_apply(db, api, input, output);

_quit:
    if (input != NULL) {
        json_decref(input);
    }
    sr_discard_changes(db->sess);
    sr_session_release_context(db->sess);
    return (ret == 0) ? 0 : -1;
}

int api_store_init(void)
{
    int ret = 0;
    pthread_t thid = {0};
     struct api_db *db = &s_api_db;
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

    ret = sr_connect(SR_CONN_CACHE_RUNNING, &db->conn);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session connect error: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = sr_session_start(db->conn, SR_DS_STARTUP, &db->sess);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session start error: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = _api_store_load(db->sess, API_YANG_MODULE);
    if (ret != 0) {
        goto _quit;
    }

    ret = sr_install_modules(db->conn, schema_paths, search_dir, NULL);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session install error: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = sr_module_change_subscribe(db->sess,
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

    LOG_DEBUG("module load success !!!!!!.");

    return 0;

_quit:
    api_store_fini();
    return -1;
}

void api_store_fini(void)
{
     struct api_db *db = &s_api_db;

    if (db->subscript != NULL) {
        sr_unsubscribe(db->subscript);
        db->subscript = NULL;
    }

    if (db->sess != NULL) {
        sr_session_stop(db->sess);
        db->sess = NULL;
    }

    if (db->conn != NULL) {
        sr_disconnect(db->conn);
        db->conn = NULL;
    }
}