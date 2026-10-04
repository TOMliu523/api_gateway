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
    static int CAT(c, _post)(void *param); \
    static PROC_INIT(200) void CAT2(c, _post, _startup)(void) { \
        api_register(API_HTTP_POST, #c, #url, CAT(c, _post)); \
    } \
    static int CAT(c, _post)(void *param)

#define API_PUT(c, url) \
    static int CAT(c, _put)(void *param); \
    static PROC_INIT(201) void CAT2(c, _put, _startup)(void) { \
        api_register(API_HTTP_PUT, #c, #url, CAT(c, _put)); \
    } \
    static int CAT(c, _put)(void *param)

#define API_PATCH(c, url) \
    static int CAT(c, _put)(void *param); \
    static PROC_INIT(202) void CAT2(c, _put, _startup)(void) { \
        api_register(API_HTTP_PATCH, #c, #url, CAT(c, _put)); \
    } \
    static int CAT(c, _put)(void *param)

#define API_GET(c, url) \
    static int CAT(c, _get)(void *param); \
    static PROC_INIT(203) void CAT2(c, _get, _startup)(void) { \
        api_register(API_HTTP_GET, #c, #url, CAT(c, _get)); \
    } \
    static int CAT(c, _get)(void *param)

#define API_DELETE(c, url) \
    static int CAT(c, _delete)(void *param); \
    static PROC_INIT(204) void CAT2(c, _delete, _startup)(void) { \
        api_register(API_HTTP_DELETE, #c, #url, CAT(c, _delete)); \
    } \
    static int CAT(c, _delete)(void *param)

struct api_param;
typedef int (*api_cb_t)(void *);

#define API_XX(XX)  \
    XX(0, GET)      \
    XX(1, POST)     \
    XX(2, PUT)      \
    XX(3, PATCH)    \
    XX(4, DELETE)

enum API_HTTP_METHOD {
#define XX(num, method) API_HTTP_##method = num,
    API_XX(XX)
#undef XX
};

enum API_ERRCODE {
    API_ERRCODE_SUCCESS = 0,

    API_ERRCODE_METHOD_NOT_SUPPORT = -1,
    API_ERRCODE_URL_NOT_EXIST = -2,
    API_ERRCODE_INTERNAL = -3,
    API_ERRCODE_NOT_FOUND = -4,
    API_ERRCODE_INVALID_ARG = -5,
    API_ERRCODE_FORMAT = -6,
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
    enum API_HTTP_METHOD method;
    const char *container;
    const char *url;
    size_t url_len;
    api_cb_t callback;
};

struct api_iface_param {
    struct api_interface *iface;
    struct api_param *param;
};

extern const struct api_interface **api_get_all_post(int *nums);
extern struct api_interface *api_get(const char *method, size_t method_len, const char *url, size_t url_len);
extern void api_register(enum API_HTTP_METHOD method, const char *container, const char *url, api_cb_t cb);

static UNUSED void api_param_set_session(void *param, void *sess)
{
    ((struct api_param *) param)->sess = sess;
}

static UNUSED void *api_param_get_session(void *param)
{
    return ((struct api_param *) param)->sess;
}

static UNUSED void api_param_set_input(void *param, void *input)
{
    ((struct api_param *) param)->json = input;
}

static UNUSED void *api_param_get_input(void *param)
{
    return ((struct api_param *) param)->json;
}

static UNUSED void api_param_set_context(void *param, void *context)
{
    ((struct api_param *) param)->context = context;
}

static UNUSED void *api_param_get_context(void *param)
{
    return ((struct api_param *) param)->context;
}

static UNUSED void api_param_set_url(void *param, const char *url)
{
    ((struct api_param *) param)->url = url;
}

static UNUSED const char *api_param_get_url(void *param)
{
    return ((struct api_param *) param)->url;
}

static UNUSED void api_param_set_output(void *param, void *output)
{
    ((struct api_param *) param)->output = output;
}

static UNUSED void *api_param_get_output(void *param)
{
    return ((struct api_param *) param)->output;
}

#endif // __API_H__