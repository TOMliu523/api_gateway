/************************************************
 * filename: api.c
 * function:
 * description:
 ***********************************************/

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <net/if.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <sysrepo.h>
#include <jansson.h>
#include <mongoose.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api.h"
#include "list.h"
#include "hash.h"
#include "errcode.h"
#include "api_inner.h"

#define API_LISTEN_BUF_LEN 32
#define API_LISTEN_HTTP_PORT 8080
#define API_LISTEN_HTTPS_PORT 8443
#define API_LISTEN_IFACE "enp3s0"
#define API_HTTP_LISTEN_FORMAT "http://%s:%d"
#define API_HTTPS_LISTEN_FORMAT "https://%s:%d"
#define API_HTTP_LOGIN_FORMAT API_HTTP_LISTEN_FORMAT "/login\r\n"
#define API_HTTPS_LOGIN_FORMAT API_HTTPS_LISTEN_FORMAT "/login\r\n"
#define API_JSON_FORMAT "Content-Type: application/json\r\n"

struct api_method {
    bool init;
    struct list_head head[API_METHOD_MAX][API_METHOD_TABLE];
};

static struct api_method s_method;
static UNUSED const char *s_tlv_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBFTCBvAIJAMNTFtpfcq8NMAoGCCqGSM49BAMCMBMxETAPBgNVBAMMCE1vbmdv\n"
    "b3NlMB4XDTI0MDUwNzE0MzczNloXDTM0MDUwNTE0MzczNlowEzERMA8GA1UEAwwI\n"
    "TW9uZ29vc2UwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASuP+86T/rOWnGpEVhl\n"
    "fxYZ+pjMbCmDZ+vdnP0rjoxudwRMRQCv5slRlDK7Lxue761sdvqxWr0Ma6TFGTNg\n"
    "epsRMAoGCCqGSM49BAMCA0gAMEUCIQCwb2CxuAKm51s81S6BIoy1IcandXSohnqs\n"
    "us64BAA7QgIgGGtUrpkgFSS0oPBlCUG6YPHFVw42vTfpTC0ySwAS0M4=\n"
    "-----END CERTIFICATE-----\n";
static UNUSED const char *s_tlv_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBMTCB2aADAgECAgkAluqkgeuV/zUwCgYIKoZIzj0EAwIwEzERMA8GA1UEAwwI\n"
    "TW9uZ29vc2UwHhcNMjQwNTA3MTQzNzM2WhcNMzQwNTA1MTQzNzM2WjARMQ8wDQYD\n"
    "VQQDDAZzZXJ2ZXIwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASo3oEiG+BuTt5y\n"
    "ZRyfwNr0C+SP+4M0RG2pYkb2v+ivbpfi72NHkmXiF/kbHXtgmSrn/PeTqiA8M+mg\n"
    "BhYjDX+zoxgwFjAUBgNVHREEDTALgglsb2NhbGhvc3QwCgYIKoZIzj0EAwIDRwAw\n"
    "RAIgTXW9MITQSwzqbNTxUUdt9DcB+8pPUTbWZpiXcA26GMYCIBiYw+DSFMLHmkHF\n"
    "+5U3NXW3gVCLN9ntD5DAx8LTG8sB\n"
    "-----END CERTIFICATE-----\n";
static UNUSED const char *s_tlv_key =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIAVdo8UAScxG7jiuNY2UZESNX/KPH8qJ0u0gOMMsAzYWoAoGCCqGSM49\n"
    "AwEHoUQDQgAEqN6BIhvgbk7ecmUcn8Da9Avkj/uDNERtqWJG9r/or26X4u9jR5Jl\n"
    "4hf5Gx17YJkq5/z3k6ogPDPpoAYWIw1/sw==\n"
    "-----END EC PRIVATE KEY-----\n";

static void _api_method_init(void)
{
    struct api_method *method = &s_method;

    if (method->init) {
        return;
    }

    for (int i = 0; i < API_METHOD_MAX; i++) {
        for (int j = 0; j < API_METHOD_TABLE; j++) {
            INIT_LIST_HEAD(&method->head[i][j]);
        }
    }

    method->init = true;
}

static int _api_register(struct list_head *head, const char *container, const char *url, size_t len, uint32_t hash,
                         api_action_fn_t update, api_action_fn_t change, api_apply_fn_t apply)
{
    int ret = 0;
    struct list_head *prev = head;
    struct api_method_node *one = NULL;
    struct api_method_node *curr = NULL;
    struct api_method_node *next = NULL;

    list_for_each_entry_safe (curr, next, head, node) {
        if (curr->hash < hash) {
            prev = &curr->node;
            continue;
        } else if (curr->hash == hash) {
            ret = strcmp(curr->url, url);
            if (ret < 0) {
                prev = &curr->node;
                continue;
            } else if (ret == 0) {
                LOG_WARN("register url(%s) exists.", url);
                return 0;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    one = malloc(sizeof(*one));
    if (one == NULL) {
        LOG_ERROR("OOM.");
        exit(EXIT_FAILURE);
    }

    INIT_LIST_HEAD(&one->node);
    one->container = container;
    one->url = url;
    one->len = len;
    one->hash = hash;
    one->update_action = update;
    one->change_action = change;
    one->apply_action = apply;

    list_add(&one->node, prev);
    return 0;
}

static const struct api_method_node *_api_get(enum API_METHOD type, const char *url, size_t len)
{
    int ret = 0;
    uint32_t hash = 0;
    struct list_head *head = NULL;
    struct api_method_node *curr = NULL;
    struct api_method_node *next = NULL;
    struct api_method *method = &s_method;

    RUNTIME_ASSERT(type < API_METHOD_MAX);

    hash_32(url, len, 0, &hash);
    head = &method->head[type][API_HASH_TABLE_INDEX(hash)];

    list_for_each_entry_safe(curr, next, head, node) {
        if (curr->hash < hash) {
            continue;
        } else if (curr->hash == hash) {
            if (curr->len != len) {
                continue;
            } else {
                ret = strncmp(curr->url, url, len);
                if (ret < 0) {
                    continue;
                } else if (ret == 0) {
                    return curr;
                } else {
                    break;
                }
            }
        } else {
            break;
        }
    }

    return NULL;
}

static void _api_set(enum API_METHOD type, const char *module, const char *url,
                     api_action_fn_t update, api_action_fn_t change, api_apply_fn_t apply)
{
    int ret = 0;
    uint32_t hash = 0;
    uint32_t url_len = 0;
    struct list_head *head = NULL;
    struct api_method *method = &s_method;

    RUNTIME_ASSERT(type < API_METHOD_MAX);

    _api_method_init();

    url_len = strlen(url);
    hash_32(url, url_len, 0, &hash);
    head = &method->head[type][API_HASH_TABLE_INDEX(hash)];

    ret = _api_register(head, module, url, url_len, hash, update, change, apply);
    if (ret < 0) {
        LOG_ERROR("post(%s) register failure. OOM.", url);
        exit(EXIT_FAILURE);
    }
}

static void api_listen_get(char *http_url, char *https_url, int len)
{
    int fd = -1;
    struct ifreq ifr = {0};
    char ip_addr[INET_ADDRSTRLEN] = "";
    const char *iface = API_LISTEN_IFACE;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_ERROR("listen get socket failure: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        LOG_ERROR("ioctl failure: %s", strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, ip_addr, INET_ADDRSTRLEN) == NULL) {
        LOG_ERROR("inet_ntop failure: %s", strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }

    snprintf(http_url, len, API_HTTP_LISTEN_FORMAT, ip_addr, API_LISTEN_HTTP_PORT);
    snprintf(https_url, len, API_HTTPS_LISTEN_FORMAT, ip_addr, API_LISTEN_HTTPS_PORT);

    close(fd);
}

static void _api_http_error(struct mg_connection *c, int errcode, struct mg_str *method, struct mg_str *uri)
{
    LOG_ERROR("%d! method: %.*s, url: %.*s", errcode, method->len, method->buf, uri->len, uri->buf);
    mg_http_reply(c, errcode, "", "");
}

static void _api_http_succ(struct mg_connection *c, struct mg_http_message *msg, void *json)
{
    char *content = NULL;
    struct mg_str *auth = NULL;
    char header[1024] = API_JSON_FORMAT;

    if (json == NULL) {
        _api_http_error(c, 500, &msg->method, &msg->uri);
        return;
    }

    content = json_dumps(json, 0);
    if (content == NULL) {
        LOG_ERROR("OOM");
        mg_http_reply(c, 500, "", "");
        return;
    }

    auth = mg_http_get_header(msg, "Authorization");
    if (auth != NULL && auth->len != 0) {
        snprintf(header, sizeof(header), "Authorization: %.*s\r\n" API_JSON_FORMAT, (int)auth->len, auth->buf);
    }

    mg_http_reply(c, 200, header, content);

    free(content);
    json_decref(json);
}

static void _api_http_redirect(struct mg_connection *c)
{
    uint16_t port = 0;
    char http_buf[128] = "";
    char buffer[INET_ADDRSTRLEN] = "";

    port = htons(c->loc.port);
    inet_ntop(AF_INET, c->loc.ip, buffer, sizeof(buffer));

    if (port == API_LISTEN_HTTPS_PORT) {
        snprintf(http_buf, sizeof(http_buf), "Location: " API_HTTPS_LOGIN_FORMAT, buffer, port);
    } else {
        snprintf(http_buf, sizeof(http_buf), "Location: " API_HTTP_LOGIN_FORMAT, buffer, port);
    }
    mg_http_reply(c, 301, http_buf, "");
}

static void _api_http_user_pwd(struct mg_connection *c)
{
    void *json = NULL;
    char *json_str = NULL;

    json = api_fail(ERRCODE_USER_PWD);
    if (json == NULL) {
        mg_http_reply(c, 500, "", "");
        return;
    }

    json_str = json_dumps(json, 0);
    if (json_str == NULL) {
        mg_http_reply(c, 500, "", "");
        goto _quit;
    }

    mg_http_reply(c, 200, API_JSON_FORMAT, json_str);

_quit:
    if (json != NULL) {
        json_decref(json);
    }
    if (json_str != NULL) {
        free(json_str);
    }
}

static void _api_http_auth(struct mg_connection *c)
{
    mg_http_reply(c, 401, "", "");
}

static void _api_response(struct mg_connection *c, struct mg_http_message *msg, enum API_STATUS code, void *rep)
{
    switch (code) {
    case API_STATUS_OK:
        _api_http_succ(c, msg, rep);
        break;

    case API_STATUS_BAD_REQUEST:
        _api_http_error(c, 400, &msg->method, &msg->uri);
        break;

    case API_STATUS_AUTH:
        _api_http_error(c, 401, &msg->method, &msg->uri);
        break;

    default:
        _api_http_error(c, 500, &msg->method, &msg->uri);
        break;
    }
}

static UNUSED int _api_login(struct mg_connection *c, struct mg_http_message *msg)
{
    enum ERRCODE code = 0;

    switch (msg->uri.len) {
    case 1:
        if (*msg->uri.buf == '/') {
            _api_http_redirect(c);
            return -1;
        }
        break;

    case 6:
        if (msg->method.len == 4 && strncasecmp(msg->method.buf, "POST", msg->method.len) == 0) {
            code = api_login(msg);
            switch (code) {
            case ERRCODE_SUCCESS:
                _api_http_succ(c, msg, api_succ(NULL));
                return 1;
            default:
                _api_http_user_pwd(c);
                return -1;
            }
        }
        FALLTHROUGH;

    default:
        code = api_refresh_login(msg);
        switch (code) {
        case ERRCODE_SUCCESS:
            return 0;
        case ERRCODE_FORBIDDEN:
            _api_http_error(c, 403, &msg->method, &msg->uri);
            return -1;
        default:
            _api_http_auth(c);
            return -1;
        }
    }

    return 0;
}

static void _api_load_cb(struct mg_connection *c, int event, void *event_data)
{
    int ret = 0;
    void *rep = NULL;
    enum API_STATUS status = 0;
    static char s_param[BUFSIZ] = "";
    struct mg_http_message *msg = NULL;
    const struct api_method_node *api = NULL;

    switch (event) {
    /*case MG_EV_ACCEPT:
        if (c->fn_data != NULL) {
            struct mg_tlv_opts opts = {
                .ca = mg_str(s_tlv_ca),
                .cert = mg_str(s_tlv_cert),
                .key = mg_str(s_tlv_key),
            };
            mg_tlv_init(c, &opts);
        }
        break;*/

    case MG_EV_HTTP_MSG:
        msg = event_data;

        // ret = _api_login(c, msg);
        if (ret != 0) {
            return;
        }

        switch (msg->method.len) {
        case 3:
            if (strncasecmp(msg->method.buf, "GET", 3) == 0) {
                api = _api_get(API_METHOD_GET, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 404, &msg->method, &msg->uri);
                    return;
                }

                strncpy(s_param, msg->query.buf, msg->query.len);
                status = api_store_query(api, s_param, msg->body.buf, msg->body.len, &rep);
                _api_response(c, msg, status, rep);
                break;
            } else if (strncasecmp(msg->method.buf, "PUT", 3) == 0) {
                api = _api_get(API_METHOD_PUT, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 404, &msg->method, &msg->uri);
                    return;
                }

                status = api_store_update(api, msg->body.buf, msg->body.len, &rep);
                _api_response(c, msg, status, rep);
                break;
            } else {
                _api_http_error(c, 405, &msg->method, &msg->uri);
            }
            break;

        case 4:
            if (strncasecmp(msg->method.buf, "POST", 4) == 0) {
                api = _api_get(API_METHOD_POST, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 404, &msg->method, &msg->uri);
                    return;
                }

                status = api_store_create(api, msg->body.buf, msg->body.len, &rep);
                _api_response(c, msg, status, rep);
                break;
            } else {
                _api_http_error(c, 405, &msg->method, &msg->uri);
            }
            break;

        case 6:
            if (strncasecmp(msg->method.buf, "DELETE", 6) == 0) {
                api = _api_get(API_METHOD_DELETE, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 404, &msg->method, &msg->uri);
                    return;
                }

                strncpy(s_param, msg->query.buf, msg->query.len);
                status = api_store_delete(api, s_param, msg->body.buf, msg->body.len, &rep);
                _api_response(c, msg, status, rep);
                break;
            } else {
                _api_http_error(c, 405, &msg->method, &msg->uri);
            }
            break;

        default:
            _api_http_error(c, 405, &msg->method, &msg->uri);
            break;
        }
        break;

    default:
        break;
    }
}

void api_post_register(const char *url, const char *module, api_action_fn_t update, api_action_fn_t change, api_apply_fn_t apply)
{
    _api_set(API_METHOD_POST, module, url, update, change, apply);
}

void api_put_register(const char *url, const char *module, api_action_fn_t update, api_action_fn_t change, api_apply_fn_t apply)
{
    _api_set(API_METHOD_PUT, module, url, update, change, apply);
}

void api_delete_register(const char *url, const char *module, api_action_fn_t action)
{
    _api_set(API_METHOD_DELETE, module, url, NULL, action, NULL);
}

void api_get_register(const char *url, const char *module, api_action_fn_t action)
{
    _api_set(API_METHOD_GET, module, url, NULL, action, NULL);
}

void *api_startup(void *arg)
{
    struct mg_mgr mgr = {0};
    struct mg_connection *c = NULL;
    char http_url[API_LISTEN_BUF_LEN] = "";
    char https_url[API_LISTEN_BUF_LEN] = "";

    pthread_setname_np(pthread_self(), "API_LISTENING");
    api_listen_get(http_url, https_url, API_LISTEN_BUF_LEN);

    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);
    if (api_store_init(arg) != 0) {
        exit(EXIT_FAILURE);
    }

    if ((c = mg_http_listen(&mgr, http_url, _api_load_cb, arg)) == NULL) {
        LOG_ERROR("Cannot to listen on %s", http_url);
        exit(EXIT_FAILURE);
    }

    if ((c = mg_http_listen(&mgr, https_url, _api_load_cb, arg)) == NULL) {
        LOG_ERROR("Cannot to listen on %s", https_url);
        exit(EXIT_FAILURE);
    }

    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    api_store_fini();
    pthread_exit(NULL);
}