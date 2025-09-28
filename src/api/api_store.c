/************************************************
 * filename: api_store.c
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
#include "type.h"
#include "hash.h"
#include "atomic.h"
#include "api_inner.h"

#define API_TIMEOUT (1000 * 60)
#define API_CONTAINER_NUMS 1024

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
        api_action_fn_t update_action;
        api_action_fn_t change_action;
        api_apply_fn_t apply_action;
        struct root *root;
        const char *url;
        void *input;
        void *output;
    } data;
};

struct api_module_order {
    const char *module_name;
    int id;
};

static struct api_db s_api_db;
static struct api_startup s_api_startup;
/*
 * String pointer array specifying the delivery order of modules.
 * All valid modules must be included in this array,
 * and modules listed earlier will be delivered before those listed later.
 */
static const char *s_module_load_order_list[] = {
    "ip4",
    "ip6",
    "route4",
    "route6",
    "arp",
    "rserver",
    "pool",
    "snat",
    "vserver",
};
static __thread char s_buffer[BUFSIZ * 4];

static void _api_store_db_log(sr_log_level_t level, const char *message)
{
    switch (level) {
    case SR_LL_ERR: LOG_ERROR("%s", message); break;
    case SR_LL_WRN: LOG_WARN("%s", message); break;
    case SR_LL_INF: LOG_INFO("%s", message); break;
    case SR_LL_DBG: LOG_DEBUG("%s", message); break;
    default: break;
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

static int _api_store_exec_call(api_action_fn_t action, void *root, const char *url, void *input, void *session, void **output)
{
    json_t *req = NULL;
    json_t *retcode = NULL;

    req = action(root, url, input, session);
    if (req == NULL) {
        return -1;
    }

    retcode = json_object_get(req, "code");
    if (retcode == NULL) {
        LOG_ERROR("return format error.");
        json_decref(req);
        return -1;
    }

    if (*output != NULL) {
        json_decref(*output);
    }
    *output = req;

    return (json_integer_value(retcode)) ? -1 : 0;
}

static int _api_store_update_cb(sr_session_ctx_t *session,
                                uint32_t sub_id,
                                const char *module_name,
                                const char *xpath,
                                sr_event_t event,
                                uint32_t operation_id,
                                void *private_data)
{
    struct private_data *data = private_data;

    switch (event) {
    case SR_EV_UPDATE:
        if (data->update_action == NULL) {
            break;
        }

        return _api_store_exec_call(data->update_action, data->root, data->url, data->input, session, &data->output);
    case SR_EV_CHANGE:
        if (data->change_action == NULL) {
            break;
        }

        return _api_store_exec_call(data->change_action, data->root, data->url, data->input, session, &data->output);
    case SR_EV_DONE:
        if (data->apply_action == NULL) {
            break;
        }

        data->apply_action(data->root, data->url, data->input, session);
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

static int _api_set_container(const char *container, const char *url, api_action_fn_t change, api_apply_fn_t apply)
{
    int id = -1;
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
                LOG_ERROR("register container(%s) exists", url);
                return -1;
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
    one->update_action = NULL;
    one->change_action = change;
    one->apply_action = apply;

    list_add(&one->node, prev);

    RUNTIME_ASSERT(startup->nums < API_CONTAINER_NUMS);

    for (int i = 0; i < ARR_NUMS(s_module_load_order_list); i++) {
        if (strcmp(s_module_load_order_list[i], container) == 0) {
            id = i;
        }
    }

    if (id == -1) {
        LOG_ERROR("Container unregister(%s) order.", container);
        return -1;
    }

    startup->container[id] = container;
    startup->nums = ARR_NUMS(s_module_load_order_list);

    return 0;
}

void api_startup_register(const char *url, const char *container, api_action_fn_t change, api_apply_fn_t apply)
{
    int ret = 0;

    ret = _api_set_container(container, url, change, apply);
    if (ret != 0) {
        exit(0);
    }
}

static int _api_store_load(sr_session_ctx_t *sess, struct api_db *db, const char *module_name)
{
    int ret = 0;
    void *json = NULL;
    void *output = NULL;
    char path[128] = "";
    const char *container = NULL;
    const struct api_method_node *api = NULL;
    const struct api_startup *startup = &s_api_startup;

    LOG_INFO("Loading config.");

    for (int i = 0; i < startup->nums; i++) {
        container = startup->container[i];
        if (container == NULL) {
            continue;
        }

        api = _api_method_get(container);
        if (api == NULL) {
            LOG_ERROR("_api_method_get get container(%s) failure.", container);
            goto _quit;
        }

        snprintf(path, sizeof(path), "/%s:%s", module_name, container);
        ret = api_db_query(path, (void **)&json);
        if (ret != 0 || json == NULL) {
            continue;
        }

        if (api->change_action != NULL) {
            ret = _api_store_exec_call(api->change_action, db->data.root, api->url, json, sess, &output);
            if (ret != 0) {
                goto _quit;
            }
        }

        if (output != NULL) {
            json_decref(output);
            output = NULL;
        }

        if (api->apply_action != NULL) {
            api->apply_action(db->data.root, api->url, json, sess);
        }

        json_decref(json); json = NULL;
    }

    LOG_DEBUG("module config load success !!!!!!.");
    return 0;

_quit:
    if (output != NULL) {
        json_decref(output);
    }
    if (json != NULL) {
        json_decref(json);
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
        LOG_ERROR("epoll_create1 failure: %s", strerror(errno));
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

static void _api_store_set(struct private_data *data, const struct api_method_node *api, const char *url, void *rep)
{
    data->update_action = api->update_action;
    data->change_action = api->change_action;
    data->apply_action = api->apply_action;
    data->input = rep;
    data->url = url;
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
        return api_succ(NULL);
    }
}

static enum API_STATUS _api_store_apply( struct api_db *db, const struct api_method_node *api, const char *url, void *input, void **output)
{
    int ret = 0;

    _api_store_set(&db->data, api, url, input);
    ret = sr_apply_changes(db->sess, API_TIMEOUT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_apply_changes failure: %s", sr_strerror(ret));
        return API_STATUS_SERVER;
    }

    *output = _api_store_get(&db->data);
    return API_STATUS_OK;
}

static int _api_store_to_json(void **obj, const char *buf, size_t len)
{
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
    json_t *subvalue = NULL;
    const char *subkey = NULL;

    n = snprintf(s_buffer, sizeof(s_buffer), "%s", key);
    json_object_foreach(value, subkey, subvalue) {
        if (json_is_string(subvalue))  {
            const char *v_str = json_string_value(subvalue);
            n += snprintf(s_buffer + n, sizeof(s_buffer) - n, "[%s='%s']", subkey, v_str);
        } else if (json_is_integer(subvalue)) {
            json_int_t value = json_integer_value(subvalue);
            n += snprintf(s_buffer + n, sizeof(s_buffer) - n, "[%s=%lld]", subkey, value);
        } else {
            LOG_ERROR("Not support json parse type.");
            return -1;
        }
    }

    ret = sr_delete_item(db->sess, s_buffer, SR_EDIT_DEFAULT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
        return -1;
    }

    return 0;
}

static int _api_store_add_create(sr_session_ctx_t *session, const struct lyd_node *node)
{
    int ret = 0;
    char *ptr = NULL;
    char *xpath = NULL;
    const char *value = NULL;

    xpath = lyd_path(node, LYD_PATH_STD, s_buffer, sizeof(s_buffer));
    if (xpath == NULL) {
        LOG_ERROR("lyd_path failure: %s", node->schema->name);
        return -1;
    }

    if (!lysc_is_key(node->schema)) {
        value = lyd_get_value(node);
        LOG_DEBUG("sr_set_item_str(session, %s, %s, NULL, SR_EDIT_DEFAULT)", xpath, value);
    } else {
        ptr = xpath + strlen(xpath);

        /*
         * To create a list entry that has only keys, use the list instance XPath
         * (with predicates) instead of the leaf path. In other words, remove the
         * trailing "/<leaf-name>" from the XPath.
         *
         * Example:
         *   Leaf path:  /v1:rserver/entrys[ip='10.10.100.80'][port='80']/port
         *   List path:  /v1:rserver/entrys[ip='10.10.100.80'][port='80']
         *   sr_set_item_str(sess, "<list-path>", NULL, NULL, SR_EDIT_DEFAULT);
         */
        for (; ptr != xpath; ptr--) {
            if (*ptr != '/') {
                continue;
            } else {
                *ptr = 0;
                break;
            }
        }

        LOG_DEBUG("sr_set_item_str(session, %s), NULL, SR_EDIT_DEFAULT", xpath);
    }

    ret = sr_set_item_str(session, xpath, value, NULL, SR_EDIT_DEFAULT);
    if (ret != 0) {
        LOG_ERROR("sr_get_item_str failure: xpath(%s), value(%s)", xpath, value);
        return -1;
    }

    return 0;
}

static int _api_store_add(sr_session_ctx_t *session, const struct lyd_node *node)
{
    int ret = 0;
    int nums = 0;
    int key_nums = 0;
    const struct lyd_node *prev = NULL;

    for (; node != NULL; node = node->next) {
        nums += 1;
        prev = node;

        if ((node->schema->nodetype & LYD_NODE_TERM) == 0) {
            ret = _api_store_add(session, lyd_child(node));
            if (ret != 0) {
                return ret;
            }
        } else if (!lysc_is_key(node->schema)) {
            ret = _api_store_add_create(session, node);
            if (ret != 0) {
                return ret;
            }
        } else {
            key_nums += 1;
        }
    }

    // Create a list containing only keys
    if (nums > 0 && nums == key_nums) {
        return _api_store_add_create(session, prev);
    }

    return 0;
}

static int _api_store_create(struct lyd_node *node, bool create)
{
    int n = 0;
    int ret = 0;
    sr_val_t *value = NULL;
    struct api_db *db = &s_api_db;
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

        return _api_store_add(db->sess, node);
    } else {
        ret = sr_edit_batch(db->sess, node, "replace");
        if (ret != SR_ERR_OK) {
            LOG_ERROR("sr_edit_batch failure: %s", sr_strerror(ret));
            return -1;
        }
    }

    return 0;
}

// Waiting for the data plane to complete initialization
static void _api_store_wait_dataplane(struct root *root)
{
    for (; !atomic_load(&root->inited););
}

enum API_STATUS api_store_create(const struct api_method_node *api, const char *buf, size_t len, void **output)
{
    int ret = 0;
    void *input = NULL;
    LY_ERR err = LY_SUCCESS;
    enum API_STATUS code = 0;
    struct lyd_node *node = NULL;
    struct api_db *db = &s_api_db;
    const struct ly_ctx *ctx = NULL;

    if (api == NULL || buf == NULL || len == 0 || output == NULL) {
        LOG_ERROR("parameter exception.");
        return API_STATUS_SERVER;
    }

    ctx = sr_session_acquire_context(db->sess);
    err = lyd_parse_data_mem(ctx, buf, LYD_JSON, LYD_PARSE_ONLY, LYD_VALIDATE_MULTI_ERROR, &node);
    if (err != LY_SUCCESS) {
        LOG_ERROR("lyd_parse_data_mem failure: %s", ly_strerr(err));
        return API_STATUS_BAD_REQUEST;
    }

    if (node == NULL) {
        LOG_ERROR("Parameter exception(Apparent success masks a hidden failure caused by schema load issues).");
        return API_STATUS_BAD_REQUEST;
    }

    // TODO Check whether the URL matches the module

    ret = _api_store_create(node, true);
    if (ret != 0) {
        code = API_STATUS_BAD_REQUEST;
        goto _quit;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        code = API_STATUS_BAD_REQUEST;
        goto _quit;
    }

    code = _api_store_apply(db, api, "", input, output);

_quit:
    if (node != NULL) {
        lyd_free_tree(node);
    }
    if (input != NULL) {
        json_decref(input);
    }
    sr_discard_changes(db->sess);
    sr_session_release_context(db->sess);
    return code;
}

enum API_STATUS api_store_update(const struct api_method_node *api, const char *buf, size_t len, void **output)
{
    int ret = 0;
    void *input = NULL;
    LY_ERR err = LY_SUCCESS;
    enum API_STATUS code = 0;
    struct lyd_node *node = NULL;
    struct api_db *db = &s_api_db;
    const struct ly_ctx *ctx = NULL;

    if (api == NULL || output == NULL) {
        LOG_ERROR("Parameter exception.");
        return API_STATUS_SERVER;
    }

    ctx = sr_session_acquire_context(db->sess);
    err = lyd_parse_data_mem(ctx, buf, LYD_JSON, LYD_PARSE_ONLY, LYD_VALIDATE_MULTI_ERROR, &node);
    if (err != LY_SUCCESS) {
        LOG_ERROR("lyd_parse_data_mem failure: %s", ly_strerr(err));
        code = API_STATUS_BAD_REQUEST;
        goto _quit;
    }

    if (node == NULL) {
        LOG_ERROR("Parameter exception(Apparent success masks a hidden failure caused by schema load issues).");
        return API_STATUS_BAD_REQUEST;
    }

    // TODO Check whether the URL matches the module

    ret = _api_store_create(node, false);
    if (ret != 0) {
        code = API_STATUS_BAD_REQUEST;
        goto _quit;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        code = API_STATUS_BAD_REQUEST;
        goto _quit;
    }

    code = _api_store_apply(db, api, "", input, output);

_quit:
    if (node != NULL) {
        lyd_free_tree(node);
    }
    if (input != NULL) {
        json_decref(input);
    }
    sr_discard_changes(db->sess);
    sr_session_release_context(db->sess);
    return code;
}

enum API_STATUS api_store_delete(const struct api_method_node *api, const char *param, const char *buf, size_t len, void **output)
{
    int ret = 0;
    int index = 0;
    void *one = NULL;
    void *iter = NULL;
    void *input = NULL;
    json_t *value = NULL;
    const char *key = NULL;
    enum API_STATUS code = 0;
    struct api_db *db = &s_api_db;

    if (api == NULL || param == NULL || buf == NULL || output == NULL) {
        LOG_ERROR("Parameter exception.");
        return API_STATUS_SERVER;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        return API_STATUS_BAD_REQUEST;
    }

    iter = json_object_iter(input);
    if (iter == NULL) {
        LOG_ERROR("OOM.");
        json_decref(input);
        return API_STATUS_SERVER;
    }

    key = json_object_iter_key(iter);
    value = json_object_iter_value(iter);
    if (json_is_array(value)) {
        int nums = json_array_size(value);
        if (nums == 0) {
            ret = sr_delete_item(db->sess, key, SR_EDIT_DEFAULT);
            if (ret != SR_ERR_OK) {
                LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
                code = API_STATUS_BAD_REQUEST;
                goto _quit;
            }
        } else {
            json_array_foreach(value, index, one) {
                ret = _api_store_delete_one(db, key, one);
                if (ret != 0) {
                    code = API_STATUS_BAD_REQUEST;
                    goto _quit;
                }
            }
        }
    } else {
        if (json_is_null(value)) {
            ret = sr_delete_item(db->sess, key, SR_EDIT_DEFAULT);
            if (ret != SR_ERR_OK) {
                LOG_ERROR("sr_delete_item failure: %s", sr_strerror(ret));
                code = API_STATUS_BAD_REQUEST;
                goto _quit;
            }
        } else {
            ret = _api_store_delete_one(db, key, one);
            if (ret != 0) {
                code = API_STATUS_BAD_REQUEST;
                goto _quit;
            }
        }
    }

    code = _api_store_apply(db, api, param, input, output);

_quit:
    if (input != NULL) {
        json_decref(input);
    }
    sr_discard_changes(db->sess);
    return code;
}

enum API_STATUS api_store_query(const struct api_method_node *api, const char *param, const char *buf, size_t len, void **output)
{
    int ret = 0;
    void *input = NULL;
    sr_val_t *val = NULL;
    struct api_db *db = &s_api_db;
    const char *format = "/v1:query/counter";

    if (api == NULL || output == NULL) {
        LOG_ERROR("Parameter exception.");
        return API_STATUS_SERVER;
    }

    ret = sr_get_item(db->sess, format, 0, &val);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_get_item failure: %s", sr_strerror(ret));
        return API_STATUS_SERVER;
    }

    val->data.uint64_val += 1;
    ret = sr_set_item(db->sess, format, val, SR_EDIT_DEFAULT);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_set_item failure: %s", sr_strerror(ret));
        return API_STATUS_SERVER;
    }

    ret = _api_store_to_json(&input, buf, len);
    if (ret != 0) {
        return API_STATUS_BAD_REQUEST;
    }

    return _api_store_apply(db, api, param, input, output);
}

int api_db_query(const char *path, void **obj)
{
    int ret = 0;
    LY_ERR err = 0;
    json_t *json = NULL;
    char *json_str = NULL;
    sr_data_t *subtree = NULL;
    struct api_db *db = &s_api_db;

    if (path == NULL || obj == NULL) {
        LOG_ERROR("path(%p) or obj(%p) is NULL", path, obj);
        return -1;
    }

    ret = sr_get_subtree(db->sess, path, 0, &subtree);
    if (ret != 0 && ret != SR_ERR_NOT_FOUND) {
        LOG_ERROR("sr_get_subtree failure: ret = %d: %s", ret, sr_strerror(ret));
        return -1;
    } else if (ret == SR_ERR_NOT_FOUND) {
        return 0;
    }

    err = lyd_print_mem(&json_str, subtree->tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS);
    if (err != LY_SUCCESS) {
        LOG_ERROR("lyd_print_mem failure: %s", ly_strerr(err));
        return -1;
    }

    ret = api_string_to_json(json_str, (void **)&json);
    if (ret != 0) {
        free(json_str);
        return -1;
    }

    if ((json_is_object(json) && json_object_size(json) == 0)
        || (json_is_array(json) && json_array_size(json) == 0)) {
        json_decref(json);
        json = NULL;
    }

    free(json_str);
    sr_release_data(subtree);

    *obj = json;
    return 0;
}

int api_store_init(void *arg)
{
    int ret = 0;
    pthread_t thid = {0};
    char buffer[1024] = "";
    char search_dir[1024] = "";
    const char *yang_path = NULL;
    struct api_db *db = &s_api_db;
    const char *module_name = "v1";
    const char *schema_path[20] = {NULL};

    sr_log_stderr(SR_LL_DBG);
    sr_log_set_cb(_api_store_db_log);
    ly_log_level(LY_LLDBG);
    ly_set_log_clb(_api_store_yang_log);

    LOG_DEBUG("sysrepo memory path: %s", sr_get_shm_path());
    LOG_DEBUG("sysrepo repo path = %s", sr_get_repo_path());

    yang_path = getenv("API_LIBYANG_PATH");
    if (yang_path == NULL) {
        LOG_ERROR("API_LIBYANG_PATH env not exists");
        return -1;
    }

    schema_path[0] = buffer;
    snprintf(buffer, sizeof(buffer), "%s/%s.yang", yang_path, module_name);
    snprintf(search_dir, sizeof(search_dir), "%s:%s/common/", yang_path, yang_path);

    db->data.root = arg;

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

    ret = sr_install_modules(db->conn, schema_path, search_dir, NULL);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("Session install error: %s", sr_strerror(ret));
        goto _quit;
    }

    ret = sr_module_change_subscribe(db->sess,
                                     module_name,
                                     NULL,
                                     _api_store_update_cb,
                                     &db->data,
                                     0,
                                     SR_SUBSCR_NO_THREAD | SR_SUBSCR_UPDATE,
                                     &db->subscript);
    if (ret != SR_ERR_OK) {
        LOG_ERROR("sr_module_change_subscribe failure: %s", sr_strerror(ret));
        goto _quit;
    }

    _api_store_wait_dataplane(db->data.root);

    ret = _api_store_load(db->sess, db, module_name);
    if (ret != 0) {
        goto _quit;
    }

    ret = pthread_create(&thid, NULL, _api_store_cb, db);
    if (ret != 0) {
        LOG_ERROR("pthread_create failure: %s", strerror(ret));
        goto _quit;
    }

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