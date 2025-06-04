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
#include "api_inner.h"

#define API_METHOD_TABLE 128
#define API_LISTEN_BUF_LEN 32
#define API_LISTEN_HTTP_PORT 8080
#define API_LISTEN_HTTPS_PORT 8443
#define API_LISTEN_IFACE "enp3s0"
#define API_LISTEN_FORMAT "%s:%d"
#define API_JSON_FORMAT "Content-Type: application/json\r\n"
#define API_HASH_TABLE_INDEX(x) ((x) % API_METHOD_TABLE)

enum API_METHOD {
    API_METHOD_POST = 0,
    API_METHOD_PUT,
    API_METHOD_DELETE,
    API_METHOD_GET,
    API_METHOD_MAX,
};

struct api_method {
    bool init;
    struct list_head head[API_METHOD_MAX][API_METHOD_TABLE];
};

static struct api_method s_method;
static const char *s_tls_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBFTCBvAIJAMNTFtpfcq8NMAoGCCqGSM49BAMCMBMxETAPBgNVBAMMCE1vbmdv\n"
    "b3NlMB4XDTI0MDUwNzE0MzczNloXDTM0MDUwNTE0MzczNlowEzERMA8GA1UEAwwI\n"
    "TW9uZ29vc2UwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASuP+86T/rOWnGpEVhl\n"
    "fxYZ+pjMbCmDZ+vdnP0rjoxudwRMRQCv5slRlDK7Lxue761sdvqxWr0Ma6TFGTNg\n"
    "epsRMAoGCCqGSM49BAMCA0gAMEUCIQCwb2CxuAKm51s81S6BIoy1IcandXSohnqs\n"
    "us64BAA7QgIgGGtUrpkgFSS0oPBlCUG6YPHFVw42vTfpTC0ySwAS0M4=\n"
    "-----END CERTIFICATE-----\n";
static const char *s_tls_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBMTCB2aADAgECAgkAluqkgeuV/zUwCgYIKoZIzj0EAwIwEzERMA8GA1UEAwwI\n"
    "TW9uZ29vc2UwHhcNMjQwNTA3MTQzNzM2WhcNMzQwNTA1MTQzNzM2WjARMQ8wDQYD\n"
    "VQQDDAZzZXJ2ZXIwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASo3oEiG+BuTt5y\n"
    "ZRyfwNr0C+SP+4M0RG2pYkb2v+ivbpfi72NHkmXiF/kbHXtgmSrn/PeTqiA8M+mg\n"
    "BhYjDX+zoxgwFjAUBgNVHREEDTALgglsb2NhbGhvc3QwCgYIKoZIzj0EAwIDRwAw\n"
    "RAIgTXW9MITQSwzqbNTxUUdt9DcB+8pPUTbWZpiXcA26GMYCIBiYw+DSFMLHmkHF\n"
    "+5U3NXW3gVCLN9ntD5DAx8LTG8sB\n"
    "-----END CERTIFICATE-----\n";
static const char *s_tls_key =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIAVdo8UAScxG7jiuNY2UZESNX/KPH8qJ0u0gOMMsAzYWoAoGCCqGSM49\n"
    "AwEHoUQDQgAEqN6BIhvgbk7ecmUcn8Da9Avkj/uDNERtqWJG9r/or26X4u9jR5Jl\n"
    "4hf5Gx17YJkq5/z3k6ogPDPpoAYWIw1/sw==\n"
    "-----END EC PRIVATE KEY-----\n";

static uint32_t _api_url_hash(const void *data, size_t len)
{
    uint32_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += ((uint8_t *)data)[i];
    }

    return sum;
}

static void api_method_init(void)
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

static int _api_register(struct list_head *head, const char *url, size_t len, uint32_t hash, api_action_fn_t action)
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
        } else if (curr->hash > hash) {
            break;
        }
    }

    one = malloc(sizeof(*one));
    if (one == NULL) {
        LOG_ERROR("OOM.");
        exit(EXIT_FAILURE);
    }

    INIT_LIST_HEAD(&one->node);
    one->url = url;
    one->len = len;
    one->hash = hash;
    one->action = action;

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

    hash = _api_url_hash(url, len);
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

static void _api_set(enum API_METHOD type, const char *url, api_action_fn_t action)
{
    int ret = 0;
    uint32_t hash = 0;
    uint32_t url_len = 0;
    struct list_head *head = NULL;
    struct api_method *method = &s_method;

    RUNTIME_ASSERT(type < API_METHOD_MAX);

    api_method_init();

    url_len = strlen(url);
    hash = _api_url_hash(url, url_len);
    head = &method->head[type][API_HASH_TABLE_INDEX(hash)];

    ret = _api_register(head, url, url_len, hash, action);
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

    snprintf(http_url, len, "http://" API_LISTEN_FORMAT, ip_addr, API_LISTEN_HTTP_PORT);
    snprintf(https_url, len, "https://" API_LISTEN_FORMAT, ip_addr, API_LISTEN_HTTPS_PORT);

    close(fd);
}

static void _api_http_error(struct mg_connection *c, int errcode, struct mg_str *method, struct mg_str *uri)
{
    static const char *s_errmsg[] = {
        [400] = "Bad Request",
        [404] = "Page not found",
        [405] = "Method Not Allowed",
        [500] = "Internal server error",
    };

    RUNTIME_ASSERT(errcode < ARR_NUMS(s_errmsg));

    if (method != NULL && uri != NULL) {
        LOG_ERROR("%d! method: %.*s, url: %.*s", errcode, method->len, method->buf, uri->len, uri->buf);
    }
    mg_http_reply(c, errcode, "", s_errmsg[errcode] ? s_errmsg[errcode] : "");
}

static void _api_http_succ(struct mg_connection *c, void *json)
{
    char *content = NULL;

    content = json_dumps(json, 0);
    if (content == NULL) {
        LOG_ERROR("OOM");
        _api_http_error(c, 500, NULL, NULL);
        return;
    }

    mg_http_reply(c, 200, API_JSON_FORMAT, content);

    free(content);
    json_decref(json);
}

static void *_api_errmsg_to_json(const char *msg)
{
    int ret = 0;
    json_t *value = NULL;
    json_t *retobj = NULL;

    retobj = json_object();
    if (retobj == NULL) {
        goto _quit;
    }

    value = json_string(msg);
    if (value == NULL) {
        goto _quit;
    }

    ret = json_object_set_new(retobj, "errmsg", value);
    if (ret < 0) {
        goto _quit;
    }

    return retobj;

_quit:
    LOG_ERROR("OOM.");
    if (value != NULL) {
        json_decref(value);
    }
    if (retobj != NULL) {
        json_decref(retobj);
    }
    return NULL;
}

static void _api_load_cb(struct mg_connection *c, int event, void *event_data)
{
    int ret = 0;
    void *rep = NULL;
    struct mg_http_message *msg = NULL;
    const struct api_method_node *api = NULL;

    switch (event) {
    case MG_EV_ACCEPT:
        if (c->fn_data != NULL) {
            struct mg_tls_opts opts = {
                .ca = mg_str(s_tls_ca),
                .cert = mg_str(s_tls_cert),
                .key = mg_str(s_tls_key),
            };
            mg_tls_init(c, &opts);
        }
        break;

    case MG_EV_HTTP_MSG:
        msg = event_data;
        switch (msg->method.len) {
        case 3:
            if (strncasecmp(msg->method.buf, "GET", 3) == 0) {
                api = _api_get(API_METHOD_GET, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 404, &msg->method, &msg->uri);
                    return;
                }

                ret = api_store_query(api, msg->body.buf, msg->body.len, &rep);
                if (ret != 0) {
                    _api_http_error(c, 500, &msg->method, &msg->uri);
                    return;
                }

                _api_http_succ(c, rep);
                return;
            } else if (strncasecmp(msg->method.buf, "PUT", 3) == 0) {
                api = _api_get(API_METHOD_PUT, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 404, &msg->method, &msg->uri);
                    return;
                }

                ret = api_store_update(api, msg->body.buf, msg->body.len, &rep);
                if (ret != 0) {
                    _api_http_error(c, 500, &msg->method, &msg->uri);
                    return;
                }

                _api_http_succ(c, rep);
                return;
            } else {
                _api_http_error(c, 405, &msg->method, &msg->uri);
            }
            break;

        case 4:
            if (strncasecmp(msg->method.buf, "POST", 4) == 0) {
                api = _api_get(API_METHOD_POST, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 500, &msg->method, &msg->uri);
                    return;
                }

                ret = api_store_create(api, msg->body.buf, msg->body.len, &rep);
                if (ret != 0) {
                    _api_http_error(c, 500, &msg->method, &msg->uri);
                    return;
                }

                _api_http_succ(c, rep);
                return;
            } else {
                _api_http_error(c, 405, &msg->method, &msg->uri);
            }
            break;

        case 6:
            if (strncasecmp(msg->method.buf, "DELETE", 6) == 0) {
                api = _api_get(API_METHOD_DELETE, msg->uri.buf, msg->uri.len);
                if (api == NULL) {
                    _api_http_error(c, 500, &msg->method, &msg->uri);
                    return;
                }

                ret = api_store_delete(api, msg->body.buf, msg->body.len, &rep);
                if (ret != 0) {
                    _api_http_error(c, 500, &msg->method, &msg->uri);
                    return;
                }

                _api_http_succ(c, rep);
                return;
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

void api_post_register(const char *url, api_action_fn_t action)
{
    _api_set(API_METHOD_POST, url, action);
}

void api_put_register(const char *url, api_action_fn_t action)
{
    _api_set(API_METHOD_PUT, url, action);
}

void api_delete_register(const char *url, api_action_fn_t action)
{
    _api_set(API_METHOD_DELETE, url, action);
}

void api_get_register(const char *url, api_action_fn_t action)
{
    _api_set(API_METHOD_GET, url, action);
}

void *api_success(void *obj)
{
    int ret = 0;
    json_t *value = NULL;
    json_t *retobj = NULL;

    retobj = json_object();
    if (retobj == NULL) {
        goto _quit;
    }

    value = json_integer((json_int_t)0);
    if (value == NULL) {
        goto _quit;
    }

    ret = json_object_set_new(retobj, "code", value);
    if (ret < 0) {
        json_decref(value);
        goto _quit;
    }

    if (obj != NULL) {
        ret = json_object_set_new(retobj, "data", obj);
        if (ret < 0) {
            goto _quit;
        }
    }

    return retobj;

_quit:
    LOG_ERROR("OOM.");
    if (retobj != NULL) {
        json_decref(retobj);
    }
    return NULL;
}

void *api_failure(int errcode, const char *errmsg)
{
    int ret = 0;
    json_t *value = NULL;
    json_t *retobj = NULL;
    json_t *errobj = NULL;

    if (errmsg == NULL) {
        LOG_ERROR("error message is NULL.");
        return NULL;
    }

    retobj = json_object();
    if (retobj == NULL) {
        goto _quit;
    }

    value = json_integer((json_int_t) errcode);
    if (value == NULL) {
        goto _quit;
    }

    ret = json_object_set_new(retobj, "code", value);
    if (ret < 0) {
        json_decref(value);
        goto _quit;
    }

    if (errmsg != NULL) {
        errobj = _api_errmsg_to_json(errmsg);
        if (errobj == NULL) {
            json_decref(value);
            goto _quit;
        }

        ret = json_object_set_new(retobj, "data", errobj);
        if (ret < 0) {
            json_decref(value);
            json_decref(errobj);
            goto _quit;
        }
    }

    return retobj;

_quit:
    LOG_ERROR("OOM.");
    if (retobj != NULL) {
        json_decref(retobj);
    }
    return NULL;
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
    if (api_store_init() != 0) {
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