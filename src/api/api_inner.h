/************************************************
 * filename: api_inner.h
 * function:
 * description:
 ***********************************************/

#ifndef __API_INNER_H__
#define __API_INNER_H__

#include "macro.h"

#define API_POST(url, func) \
    static PROC_INIT void func##_register(func, #url) { \
        api_post_register(#url, func); \
    } \
    static void *func(char *url, void *json)

#define API_DELETE(url, func) \
    static PROC_INIT void func##_register(func, #url) { \
        api_delete_register(#url, func); \
    } \
    static void *func(char *url, void *json)

#define API_GET(url, func) \
    static PROC_INIT void func##_register(func, #url) { \
        api_get_register(#url, func) \
    } \
    static void *func(char *url, void *json)

/*
 * @param1: request url
 * @param2: request body(format: json)
 * @return: json
 */
typedef void *(*api_action_t)(const char *, void *);

extern void api_post_register(const char *url, api_action_t cb);
extern void api_delete_register(const char *url, api_action_t cb);
extern void api_get_register(const char *url, api_action_t cb);
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

#endif // __API_INNER_H__