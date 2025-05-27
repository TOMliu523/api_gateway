/************************************************
 * filename: api.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <jansson.h>
#include <mongoose.h>

#include "log.h"
#include "api.h"
#include "api_inner.h"

#define API_LISTEN_BUF_LEN 32
#define API_LISTEN_HTTP_PORT 8080
#define API_LISTEN_HTTPS_PORT 8443
#define API_LISTEN_IFACE "enp3s0"
#define API_LISTEN_FORMAT "%s:%d"
#define API_JSON_FORMAT "Content-Type: application/json\r"

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

static void api_cb(struct mg_connection *c, int event, void *event_data)
{
    int ret = 0;
    const char *err_msg = NULL;
    struct mg_http_message *msg = NULL;
    static __thread char *content = NULL;
    static __thread const char *err_404_msg = "{\"code\":\"1\",\"message\":\"Endpoint not found\"}";
    static __thread const char *err_405_msg = "{\"code\":\"1\",\"message\":\"Method Not Allowed\"}";

    switch (event) {
    case MG_EV_ACCEPT:
        break;

    case MG_EV_HTTP_MSG:
        msg = event_data;
        switch (msg->method.len) {
        case 3:
            if (strncasecmp(msg->method.buf, "GET", 3) == 0) {

            } else if (strncasecmp(msg->method.buf, "PUT", 3) == 0) {
            } else {
                goto _rep405;
            }
            break;

        case 4:
            if (strncasecmp(msg->method.buf, "POST", 4) == 0) {

            } else {
                goto _rep405;
            }
            break;

        case 6:
            if (strncasecmp(msg->method.buf, "DELETE", 6) == 0) {

            } else {
                goto _rep405;
            }
            break;

        default: _rep405:
            LOG_ERROR("405! method: %.*s, url: %s", msg->method.len, msg->method.buf, msg->uri.len, msg->uri.buf);
            mg_http_reply(c, 405, API_JSON_FORMAT, err_405_msg);
            break;
        }
        break;

    default:
        break;
    }

    return;

_rep404:
    LOG_ERROR("404! method: %.*s, url: %s", msg->method.len, msg->method.buf, msg->uri.len, msg->uri.buf);
    mg_http_reply(c, 404, API_JSON_FORMAT, err_404_msg);
    return;
}

void api_post_register(const char *url, api_cb_t cb)
{

}

void api_delete_register(const char *url, api_cb_t cb)
{

}

void api_get_register(const char *url, api_cb_t cb)
{

}

void *api_startup(void *arg)
{
    struct mg_mgr mgr = {0};
    struct mg_connection *c = NULL;
    char http_url[API_LISTEN_BUF_LEN] = "";
    char https_url[API_LISTEN_BUF_LEN] = "";

    api_listen_get(http_url, https_url, API_LISTEN_BUF_LEN);

    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);

    if ((c = mg_http_listen(&mgr, http_url, api_cb, arg)) == NULL) {
        LOG_ERROR("Cannot to listen on %s", http_url);
        exit(EXIT_FAILURE);
    }

    if ((c = mg_http_listen(&mgr, https_url, api_cb, arg)) == NULL) {
        LOG_ERROR("Cannot to listen on %s", https_url);
        exit(EXIT_FAILURE);
    }

    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    pthread_exit(NULL);
}