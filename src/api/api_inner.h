/************************************************
 * filename: api_inner.h
 * function:
 * description:
 ***********************************************/

#ifndef __API_INNER_H__
#define __API_INNER_H__

#include <stdint.h>

#include "log.h"
#include "list.h"
#include "macro.h"

#define API_POST(_url, _func) \
    static void *CAT(_func, _post)(const char *url, void *json, void *session); \
    static PROC_INIT void CAT2(_func, _post, _startup)(void) { \
        api_post_register(#_url, CAT(_func, _post)); \
    } \
    static void *CAT(_func, _post)(const char *url, void *json, void *session)

#define API_PUT(_url, _func) \
    static void *CAT(_func, _put)(const char *url, void *json, void *session); \
    static PROC_INIT void _func##_put##_startup(void) { \
        api_put_register(#_url, CAT(_func, _put)); \
    } \
    static void *_func##_put(const char *url, void *json, void *session)

#define API_DELETE(_url, _func) \
    static void *CAT(_func, _delete)(const char *url, void *json, void *session); \
    static PROC_INIT void _func##_delete##_startup(void) { \
        api_delete_register(#_url, CAT(_func, _delete)); \
    } \
    static void *_func##_delete(const char *url, void *json, void *session)

#define API_GET(_url, _func) \
    static void *CAT(_func, _get)(const char *url, void *json, void *session); \
    static PROC_INIT void _func##_get##_startup(void) { \
        api_get_register(#_url, CAT(_func, _get)); \
    } \
    static void *_func##_get(const char *url, void *json, void *session)

/*
 * @param1: request url
 * @param2: request body(format: json)
 * @return: json
 */
typedef void *(*api_action_fn_t)(const char *, void *, void *);

struct api_method_node {
    struct list_head node;
    const char *url;
    size_t len;
    uint32_t hash;
    api_action_fn_t action;
};

extern void api_post_register(const char *url, api_action_fn_t cb);
extern void api_put_register(const char *url, api_action_fn_t cb);
extern void api_delete_register(const char *url, api_action_fn_t cb);
extern void api_get_register(const char *url, api_action_fn_t cb);
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
extern void *api_success(void *obj);

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
extern void *api_failure(int errcode, const char *errmsg);

extern int api_store_init(void);
extern void api_store_fini(void);
extern int api_store_query(const struct api_method_node *api, const char *buf, size_t len, void **req);
extern int api_store_update(const struct api_method_node *api, const char *buf, size_t len, void **req);
extern int api_store_create(const struct api_method_node *api, const char *buf, size_t len, void **req);
extern int api_store_delete(const struct api_method_node *api, const char *buf, size_t len, void **req);

#endif // __API_INNER_H__