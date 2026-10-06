/*****************************************************************************
 * filename: api.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __API_H__
#define __API_H__

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "list.h"
#include "macro.h"

#define API_POST(c, url) \
    static int CAT(c, _post)(void *param); \
    static PROC_INIT(200) void CAT2(c, _post, _startup)(void) { \
        api_register(API_HTTP_POST, #c, #url, CAT(c, _post)); \
    } \
    static int CAT(c, _post)(void *param)

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

#define API_OUTPUT_LEN (1024 * 1024)
#define API_RETURN_ERROR "{\"code\":%d, \"message\":\"%s\"}"
#define API_RETURN_SUCCESS "{\"code\":0, \"data\":%s}"

struct api_param;
typedef int (*api_cb_t)(void *);

#define API_XX(XX)  \
    XX(0, GET)      \
    XX(1, POST)     \
    XX(2, PATCH)    \
    XX(3, DELETE)

enum API_HTTP_METHOD {
#define XX(num, method) API_HTTP_##method = num,
    API_XX(XX)
#undef XX
};

enum API_STATUS {
    API_STATUS_SUCCESS = 0,

    API_STATUS_METHOD_NOT_SUPPORT = -1,
    API_STATUS_URL_NOT_EXIST = -2,
    API_STATUS_INTERNAL = -3,
    API_STATUS_NOT_FOUND = -4,
    API_STATUS_INVALID_ARG = -5,
    API_STATUS_FORMAT = -6,
    API_STATUS_EXIST = -7,
};

enum API_ERRCODE {
    API_ERRCODE_SUCCESS,

    API_ERRCODE_ARP_DB = 100,
    API_ERRCODE_ARP_FORMAT = 101,
};

struct api_param {
    void *sess;
    void *json;
    void *context;
    const char *url;
    char output[API_OUTPUT_LEN];
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

static UNUSED void api_param_set_error(void *param, int code, const char *output)
{
    char *out = ((struct api_param *) param)->output;
    snprintf(out, API_OUTPUT_LEN, API_RETURN_ERROR, code, output);
}

static UNUSED void api_param_set_success(void *param, const char *output)
{
    char *out = ((struct api_param *) param)->output;
    snprintf(out, API_OUTPUT_LEN, API_RETURN_SUCCESS, output);
}

static UNUSED void *api_param_get_output(void *param)
{
    return ((struct api_param *) param)->output;
}

#endif // __API_H__