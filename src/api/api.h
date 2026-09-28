/*****************************************************************************
 * filename: api.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __API_H__
#define __API_H__

#include <stdint.h>

#include "list.h"
#include "macro.h"

#define API_POST(c, url) \
    static void *CAT(c, _post)(void *context, struct api_param *param); \
    static PROC_INIT(200) void CAT2(c, _post, _startup)(void) { \
        api_register(API_POST, #c, #url, CAT(c, _post)); \
    } \
    static void *CAT(c, _post)(void *context, struct api_param *param)

#define API_PUT(c, url) \
    static void *CAT(c, _put)(void *context, struct api_param *param); \
    static PROC_INIT(201) void CAT2(c, _put, _startup)(void) { \
        api_register(API_PUT, #c, #url, CAT(c, _put)); \
    } \
    static void *CAT(c, _put)(void *context, struct api_param *param)

#define API_GET(c, url) \
    static void *CAT(c, _get)(void *context, struct api_param *param); \
    static PROC_INIT(201) void CAT2(c, _get, _startup)(void) { \
        api_register(API_GET, #c, #url, CAT(c, _get)); \
    } \
    static void *CAT(c, _get)(void *context, struct api_param *param)

#define API_DELETE(c, url) \
    static void *CAT(c, _delete)(void *context, struct api_param *param); \
    static PROC_INIT(201) void CAT2(c, _delete, _startup)(void) { \
        api_register(API_DELETE, #c, #url, CAT(c, _delete)); \
    } \
    static void *CAT(c, _delete)(void *context, struct api_param *param)

struct api_param;
typedef void *(*api_cb_t)(void *, struct api_param *);

#define API_XX(XX)  \
    XX(0, GET)      \
    XX(1, POST)     \
    XX(2, PUT)      \
    XX(3, DELETE)

enum API_METHOD {
#define XX(num, method) API_##method = num,
    API_XX(XX)
#undef XX
};

struct api_param {
    void *sess;
    void *json;
    const char *url;
};

struct api_interface {
    struct list_head node;
    uint32_t hash;
    enum API_METHOD method;
    const char *container;
    const char *url;
    api_cb_t callback;
};

extern struct api_interface *api_get(enum API_METHOD method, const char *url);
extern void api_register(enum API_METHOD method, const char *container, const char *url, api_cb_t cb);

#endif // __API_H__