/*****************************************************************************
 * filename: api.c
 * function:
 * description:
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "api.h"

#define API_HASH_BUCKET 1024
#define API_IDX(h) ((h) % API_HASH_BUCKET)

static struct list_head s_api_head[API_HASH_BUCKET];

static PROC_INIT(102) void _api_init(void)
{
    for (int i = 0; i < API_HASH_BUCKET; i++) {
        INIT_LIST_HEAD(&s_api_head[i]);
    }
}

static int _api_hash(enum API_METHOD method, const char *url)
{
    uint32_t hash = 0;

    hash = (uint32_t)method;

    for (int i = 0; url[i] != 0; i++) {
        hash += url[i];
    }

    return hash;
}

struct api_interface *api_get(enum API_METHOD method, const char *url)
{
    uint32_t hash = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct list_head *head = NULL;
    struct api_interface *iface = NULL;

    hash = _api_hash(method, url);
    head = &s_api_head[API_IDX(hash)];

    list_for_each_prev_safe(pos, next, head) {
        iface = (struct api_interface *) pos;
        if (iface->hash == hash && iface->method == method && strcmp(iface->url, url) == 0) {
            return iface;
        }
    }

    return NULL;
}

void api_register(enum API_METHOD method, const char *container, const char *url, api_cb_t cb)
{
    uint32_t idx = 0;
    struct list_head *head = NULL;
    struct api_interface *iface = NULL;
    static const char *api_method_string[] = {
#define XX(num, method) [num] = #method,
        API_XX(XX)
#undef XX
    };

    if (container == NULL || url == NULL || cb == NULL) {
        const char *tmp1 = container != NULL ? container : "null";
        const char *tmp2 = url != NULL ? url : "null";
        LOG_ERROR("container: %s, url: %s, cb: %p",  tmp1, tmp2, cb);
        exit(EXIT_FAILURE);
    }

    iface = api_get(method, url);
    if (iface != NULL) {
        LOG_ERROR("method: %s, url: %s exists", api_method_string[method], url);
        exit(EXIT_FAILURE);
    }

    iface = malloc(sizeof(*iface));
    if (iface == NULL) {
        LOG_ERROR("OOM.");
        exit(EXIT_FAILURE);
    }

    memset(iface, 0, sizeof(*iface));
    iface->method = method;
    iface->container = container;
    iface->url = url;
    iface->callback = cb;
    INIT_LIST_HEAD(&iface->node);

    iface->hash = _api_hash(method, url);
    idx = API_IDX(iface->hash);
    head = &s_api_head[idx];

    list_add(&iface->node, head);

    LOG_INFO("(%d, %s, %s) SUCCESS", method, container, url);
}