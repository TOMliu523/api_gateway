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

typedef void (*api_cb_t)(char *, void *);

extern void api_post_register(const char *url, api_cb_t cb);
extern void api_delete_register(const char *url, api_cb_t cb);
extern void api_get_register(const char *url, api_cb_t cb);

#endif // __API_INNER_H__