/*****************************************************************************
 * filename: api_http.h
 * function:
 * description:
 ****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>

#include <jansson.h>
#include <mongoose.h>

#include "log.h"
#include "api.h"
#include "type.h"
#include "api_http.h"

#define URL_LEN 512
#define API_APP_JSON "Content-Type: application/json\r\n"

static UNUSED const char *s_code_to_msg[] = {
    [100] = "Continue",
    [101] = "Switching Protocols",
    [102] = "Processing",
    [103] = "Early Hints",
    [200] = "OK",
    [201] = "Created",
    [202] = "Accepted",
    [300] = "Multiple Choices",
    [301] = "Moved Permanently",
    [302] = "Found",
    [400] = "Bad Request",
    [401] = "401",
    [402] = "Payment Required",
    [403] = "Forbidden",
    [404] = "Not Found",
    [405] = "Method Not Allowed",
    [500] = "Internal Server Error",
    [501] = "Not Implemented",
    [502] = "Bad Gateway",
    [503] = "Service Unavailable",
};

static void _api_http_reply(struct mg_connection *c, int code, const char *msg)
{
    static __thread char buffer[BUFSIZ] = "";

    snprintf(buffer, sizeof(buffer), "{\"code\": %d, \"message\":%s}", code, msg);
    mg_http_reply(c, code, API_APP_JSON, buffer);
}

static void _api_http_task(struct mg_connection *c, int ev, void *ev_data)
{
    /*int ret = 0;
    struct context *context = c->fn_data;*/

    /*if (ev == MG_EV_ACCEPT && c->is_tls) {
        struct mg_tls_opts opts = {
            .cert = mg_file_read(&mg_fs_posix, s_cert),
            .key  = mg_file_read(&mg_fs_posix, s_key),
        };

        // mg_tls_init(c, &opts);
    }*/

    if (ev == MG_EV_HTTP_MSG) {
        struct api_interface *iface = NULL;
        struct mg_http_message *hm = ev_data;

        iface = api_get(hm->method.buf, hm->method.len, hm->uri.buf, hm->uri.len);
        if (iface == NULL) {
            _api_http_reply(c, 404, "");
            return;
        }

        _api_http_reply(c, 200, "OK");
        // api->callback();
    }
}

void *api_http(void *arg)
{
    char url[URL_LEN] = {0};
    struct mg_mgr mgr = {0};
    struct mg_connection *conn = NULL;
    struct context *context = (struct context *)arg;

    pthread_setname_np(pthread_self(), "API_HTTP");
    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);

    if (context->config.http_port > 0) {
        snprintf(url, sizeof(url), "http://%s:%d", context->config.address, context->config.http_port);
        LOG_INFO("HTTP: %s", url);

        conn = mg_http_listen(&mgr, url, _api_http_task, context);
        if (conn == NULL) {
            LOG_ERROR("Function(mg_http_listen) failure");
            goto _quit;
        }
    }

    if (context->config.https_port > 0) {
        snprintf(url, sizeof(url), "https://%s:%d", context->config.address, context->config.https_port);
        LOG_INFO("HTTPS: %s", url);

        conn = mg_http_listen(&mgr, url, _api_http_task, context);
        if (conn == NULL) {
            LOG_ERROR("Function(mg_https_listen) failure");
            goto _quit;
        }
    }

    for (;;) {
        mg_mgr_poll(&mgr, 100);
    }

    mg_mgr_free(&mgr);
    pthread_exit(NULL);

_quit:
    mg_mgr_free(&mgr);
    pthread_exit(NULL);
}