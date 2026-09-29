/*****************************************************************************
 * filename: api.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __API_H__
#define __API_H__

#include <stdint.h>
#include <jansson.h>

#include "list.h"
#include "macro.h"

#define API_POST(c, url) \
    static int CAT(c, _post)(void *param); \
    static PROC_INIT(200) void CAT2(c, _post, _startup)(void) { \
        api_register(API_POST, #c, #url, CAT(c, _post)); \
    } \
    static int CAT(c, _post)(void *param)

#define API_PUT(c, url) \
    static int CAT(c, _put)(void *param); \
    static PROC_INIT(201) void CAT2(c, _put, _startup)(void) { \
        api_register(API_PUT, #c, #url, CAT(c, _put)); \
    } \
    static int CAT(c, _put)(void *param)

#define API_GET(c, url) \
    static int CAT(c, _get)(void *param); \
    static PROC_INIT(201) void CAT2(c, _get, _startup)(void) { \
        api_register(API_GET, #c, #url, CAT(c, _get)); \
    } \
    static int CAT(c, _get)(void *param)

#define API_DELETE(c, url) \
    static int CAT(c, _delete)(void *param); \
    static PROC_INIT(201) void CAT2(c, _delete, _startup)(void) { \
        api_register(API_DELETE, #c, #url, CAT(c, _delete)); \
    } \
    static int CAT(c, _delete)(void *param)

struct api_param;
typedef int (*api_cb_t)(void *);

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
    void *output;
    void *context;
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

static UNUSED void *api_param_get_session(void *param)
{
    return ((struct api_param *) param)->sess;
}

static UNUSED void *api_param_get_input(void *param)
{
    return ((struct api_param *) param)->json;
}

static UNUSED void *api_param_get_context(void *param)
{
    return ((struct api_param *) param)->context;
}

static UNUSED const char *api_param_get_url(void *param)
{
    return ((struct api_param *) param)->url;
}

static UNUSED void *api_param_get_output(void *param)
{
    return ((struct api_param *) param)->output;
}

static UNUSED void api_param_set_output(void *param, void *output)
{
    void *old_output = NULL;

    old_output = ((struct api_param *) param)->output;
    if (old_output != NULL) {
        json_decref(old_output);
    }

    ((struct api_param *) param)->output = output;
}

#endif // __API_H__