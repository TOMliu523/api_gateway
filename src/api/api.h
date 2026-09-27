/*****************************************************************************
 * filename: api.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __API_H__
#define __API_H__

#include <stdint.h>

#include "macro.h"

#define API_POST(c, url) \
    static void *CAT(c, _post)(void *context, struct api_param *param); \
    static PROC_INIT(200) void CAT2(c, _post, __startup)(void) { \
        api_register(API_POST, #c, url, CAT(c, _POST)); \
    } \
    static void *CAT(c, _post)(void *context, struct api_param *param)

struct api_param;
typedef void *(*api_cb_t)(void *, struct api_param *);

enum API_METHOD {
    API_POST,
    API_GET,
    API_PUT,
    API_DELETE,
    API_MAX,
};

struct api_param {
    void *sess;
    void *json;
    const char *url;
};

#endif // __API_H__