/************************************************
 * filename: api_inner.h
 * function:
 * description:
 ***********************************************/

#ifndef __API_INNER_H__
#define __API_INNER_H__

#include <stdint.h>

#include <jansson.h>

#include "list.h"
#include "macro.h"

#define API_METHOD_TABLE 512
#define API_AUTH "Authorization"
#define API_HASH_TABLE_INDEX(x) ((x) % API_METHOD_TABLE)

/*
 * Automatically loads configuration at startup
 *
 * The POST interface can only use a JSON object as the source for configuration delivery,
 * which allows the configuration to be automatically applied during process startup.
 */
#define API_POST(uri, container) \
    static void *CAT(container, _post)(void *cfg, const char *, void *, void *); \
    static PROC_INIT void CAT2(container, _post, _startup)(void) { \
        api_post_register(#uri, #container, NULL, CAT(container, _post), NULL); \
        api_startup_register(#uri, #container, CAT(container, _post), NULL); \
    } \
    static void *CAT(container, _post)(void *cfg, const char *url, void *json, void *sess)

// Configuration not loaded at startup
#define API_POST_NO_LOAD(uri, container) \
    static void *CAT(container, _post)(void *cfg, const char *, void *, void *); \
    static PROC_INIT void CAT2(container, _post, _startup)(void) { \
        api_post_register(#uri, container, NULL, CAT(container, _post), NULL); \
    } \
    static void *CAT(container, _post)(void *cfg, const char *url, void *json, void *sess)

/*
 * Automatically loads configuration at startup
 *
 * Description of fn1, fn2, and fn3:
 * fn1 is executed before the configuration is saved. It can be used to modify the submitted
 * data (e.g., sanitize or hash passwords).
 * If it returns an error, the submission will be rejected.
 * fn2 is used to load existing configuration. It operates in read-only mode, and the configuration
 * must not be changed.
 * If it returns an error, the configuration cannot be saved.
 * fn3 is used to persist the configuration. It is expected to succeed and should not return errors.
 */
#define API_POST_REGISTER(uri, container, fn1, fn2, fn3) \
    static PROC_INIT void CAT2(__, container, _post_register)(void) { \
        api_post_register(#uri, #container, fn1, fn2, fn3); \
        api_startup_register(#uri, #container, fn2, fn3); \
    }

// Configuration not loaded at startup
#define API_POST_NO_LOAD_REGISTER(uri, container, fn1, fn2, fn3) \
    static PROC_INIT void CAT2(__, container, _post_no_load_register)(void) { \
        api_post_register(#uri, #container, fn1, fn2, fn3); \
    }

#define API_PUT(uri, container) \
    static void *CAT(container, _put)(void *cfg, const char *, void *, void *); \
    static PROC_INIT void CAT2(container, _put, _startup)(void) { \
        api_put_register(#uri, #container, NULL, CAT(container, _put), NULL); \
    } \
    static void *container##_put(void *cfg, const char *url, void *json, void *sess)

#define API_PUT_REGISTER(uri, container, fn1, fn2, fn3) \
    static PROC_INIT void CAT2(__, container, _put)(void) { \
        api_put_register(#uri, #container, fn1, fn2, fn3); \
    }

#define API_DEL(uri, container) \
    static void *CAT(container, _delete)(void *cfg, const char *, void *, void *); \
    static PROC_INIT void CAT2(container, _delete, _startup)(void) { \
        api_delete_register(#uri, #container, CAT(container, _delete)); \
    } \
    static void *container##_delete(void *cfg, const char *url, void *json, void *sess)

#define API_GET(uri, container) \
    static void *CAT(container, _get)(void *cfg, const char *, void *, void *); \
    static PROC_INIT void CAT2(container, _get, _startup)(void) { \
        api_get_register(#uri, #container, CAT(container, _get)); \
    } \
    static void *CAT(container, _get)(void *cfg, const char *url, void *json, void *sess)

enum API_METHOD {
    API_METHOD_POST = 0,
    API_METHOD_PUT,
    API_METHOD_DELETE,
    API_METHOD_GET,
    API_METHOD_MAX,
};

enum API_STATUS {
    API_STATUS_OK = 200,
    API_STATUS_REDIRECT = 300,
    API_STATUS_BAD_REQUEST = 400,
    API_STATUS_AUTH = 401,
    API_STATUS_FORBIDDEN = 403,
    API_STATUS_NOT_FOUND = 404,
    API_STATUS_SERVER = 500,
};

struct api_user {
    const char username[32];
    const char password[128];
    enum ROLE_TYPE {
        SYSTEM_ROOT,
        SYSTEM_ADMIN,
        SYSTEM_AUDIT
    } type;
};

/*
 * @param1: request url
 * @param2: request body(format: json)
 * @return: json
 */
typedef void *(*api_action_fn_t)(void *, const char *, void *, void *);
typedef void (*api_apply_fn_t)(void *, const char *, void *, void *);

struct api_method_node {
    struct list_head node;
    const char *container;
    const char *url;
    size_t len;
    uint32_t hash;
    api_action_fn_t update_action;
    api_action_fn_t change_action;
    api_apply_fn_t apply_action;
};

extern enum ERRCODE api_login(void *arg);
extern enum ERRCODE api_refresh_login(void *arg);

extern void api_post_register(const char *, const char *, api_action_fn_t, api_action_fn_t, api_apply_fn_t);
extern void api_put_register(const char *, const char *, api_action_fn_t, api_action_fn_t, api_apply_fn_t);
extern void api_delete_register(const char *, const char *, api_action_fn_t);
extern void api_get_register(const char *, const char *, api_action_fn_t);
extern void api_startup_register(const char *, const char *, api_action_fn_t, api_apply_fn_t);
/*
 * {
 *     "code" : 0,
 *     "data" : {
 *         $obj_json_string
 *     }
 * }
 * @param: obj NULL or json object
 * @return: json object or NULL
 */
extern void *api_succ(void *obj);

/*
 * {
 *     "code" : 0,
 *     "data" : {
 *         "errmsg" : errmsg
 *     }
 * }
 * @param: errcode non-zero
 * @param: errmsg error message
 * @param: json object or NULL
 */
extern void *api_fail_msg(int errcode, const char *errmsg);

// Use the default message for errmsg, same as api_fail_msg
extern void *api_fail(int errcode);

extern int api_store_init(void *arg);
extern void api_store_fini(void);

extern enum API_STATUS api_store_query(const struct api_method_node *api, const char *param, const char *buf, size_t len, void **req);
extern enum API_STATUS api_store_update(const struct api_method_node *api, const char *buf, size_t len, void **req);
extern enum API_STATUS api_store_create(const struct api_method_node *api, const char *buf, size_t len, void **req);
extern enum API_STATUS api_store_delete(const struct api_method_node *api, const char *param, const char *buf, size_t len, void **req);

extern int api_db_query(const char *path, void **obj);

extern int api_account_desensitize(char *dst, size_t max, const char *passwd, size_t len);
extern int api_string_to_json(const char *string, void **json);
extern int api_json_to_string(void *json, char **string);
extern int api_json_add_string(void *json, const char *name, const char *value);
extern int api_json_add_integer(void *json, const char *name, json_int_t value);

extern void api_numa_config_update(void *, void **[], void *[], void (*)(void *[], int));

extern void *api_malloc(size_t);
extern void *api_malloc_numa(size_t, int);
extern void api_free(void *);

static INLINE void *api_v1_modify_list(void *json, const char *module_name, const char *list_name)
{
    char buffer[256] = "";

    snprintf(buffer, sizeof(buffer), "v1:%s", module_name);
    return json_object_get(json_object_get(json, buffer), list_name);
}

static INLINE void *api_v1_delete_list(void *json, const char *module_name, const char *list_name)
{
    char buffer[256] = "";

    snprintf(buffer, sizeof(buffer), "/v1:%s/%s", module_name, list_name);
    return json_object_get(json, buffer);
}

#endif // __API_INNER_H__