/*****************************************************************************
 * filename: api_http.h
 * function:
 * description:
 ****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>

#include <mongoose.h>

#include "log.h"
#include "api.h"
#include "top.h"
#include "config.h"
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

static void _api_http_reply(struct mg_connection *c, int code, void *output)
{
    mg_http_reply(c, code, API_APP_JSON, output);
}

static INLINE int _api_status_to_http_status(enum API_STATUS errcode)
{
    switch (errcode) {
    default:
        return 200;
    case API_STATUS_METHOD_NOT_SUPPORT:
    case API_STATUS_URL_NOT_EXIST:
    case API_STATUS_EXIST:
        return 404;
    case API_STATUS_INTERNAL:
        return 500;
    }
}

static void _api_http_task(struct mg_connection *c, int ev, void *ev_data)
{
    int ret = 0;
    void *param = NULL;
    void *output = NULL;
    struct api_iface_param *iface_param = c->fn_data;

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
            _api_http_reply(c, 404, NULL);
            return;
        }

        iface_param->iface = iface;
        param = iface_param->param;
        api_param_set_url(param, iface->url);

        switch (iface->method) {
        case API_HTTP_POST:
            ret = config_post(param, hm->body.buf, hm->body.len);
            break;
        case API_HTTP_PATCH:
            ret = config_patch(param, hm->body.buf, hm->body.len);
            break;
        case API_HTTP_DELETE:
            ret = config_delete(param, hm->body.buf, hm->body.len);
            break;
        case API_HTTP_GET:
            ret = config_get(param, hm->body.buf, hm->body.len);
            break;
        default:
            ret = API_STATUS_METHOD_NOT_SUPPORT;
            break;
        }

        output = api_param_get_output(param);
        _api_http_reply(c, _api_status_to_http_status(ret), output);
        config_param_clean(param);
    }
}

void *api_http(void *arg)
{
    int ret = 0;
    char url[URL_LEN] = {0};
    struct mg_mgr mgr = {0};
    struct api_param *param = {0};
    struct mg_connection *conn = NULL;
    struct api_iface_param iface_param = {0};
    struct context *context = (struct context *)arg;
    struct boot_config *config = &context->config;

    param = malloc(sizeof(*param));
    if (param == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    iface_param.param = param;

    pthread_setname_np(pthread_self(), "API_HTTP");
    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);

    api_param_set_session(param, config->session);
    api_param_set_context(param, context);

    ret = config_subscript(&iface_param);
    if (ret != 0) {
        goto _quit;
    }

    if (config->http_port > 0) {
        snprintf(url, sizeof(url), "http://%s:%d", config->address, config->http_port);
        LOG_INFO("HTTP: %s", url);

        conn = mg_http_listen(&mgr, url, _api_http_task, &iface_param);
        if (conn == NULL) {
            LOG_ERROR("Function(mg_http_listen) failure");
            goto _quit;
        }
    }

    if (config->https_port > 0) {
        snprintf(url, sizeof(url), "https://%s:%d", config->address, config->https_port);
        LOG_INFO("HTTPS: %s", url);

        conn = mg_http_listen(&mgr, url, _api_http_task, &iface_param);
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
    free(param);
    exit(EXIT_FAILURE);
}