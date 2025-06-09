/************************************************
 * filename: api_login.c
 * function:
 * description:
 ***********************************************/

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <mongoose.h>

#include "macro.h"
#include "api_inner.h"

enum API_ERRCODE api_login(void *arg)
{
    struct mg_str *auth = NULL;
    struct mg_http_message *msg = arg;

    auth = mg_http_get_header(msg, "Authorization");
    LOG_ERROR("Authorization = %.*s", auth->len, auth->buf);

    auth->buf = "===============================";
    auth->len = strlen(auth->buf);
    auth = mg_http_get_header(msg, "Authorization");
    LOG_ERROR("Authorization = %.*s", auth->len, auth->buf);

    return API_ERRCODE_SUCCESS;
}

enum API_ERRCODE api_refresh_login(void *arg)
{
    return API_ERRCODE_SUCCESS;
}