/*****************************************************************************
 * filename: api.c
 * function:
 * description:
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "log.h"
#include "api.h"

#define API_HASH_BUCKET 1024
#define API_IDX(h) ((h) % API_HASH_BUCKET)

static const char *s_api_method_string[] = {
#define XX(num, method) [num] = #method,
    API_XX(XX)
#undef XX
};
static struct list_head s_api_head[API_HASH_BUCKET];
static int s_api_interface_idx = 0;
static struct api_interface *s_api_interface[API_HASH_BUCKET] = {NULL};

static PROC_INIT(102) void _api_init(void)
{
    for (int i = 0; i < API_HASH_BUCKET; i++) {
        INIT_LIST_HEAD(&s_api_head[i]);
    }
}

static int _api_hash(enum API_HTTP_METHOD method, const char *url, size_t len)
{
    uint32_t hash = 0;

    hash = (uint32_t)method;

    for (int i = 0; i < len; i++) {
        hash += url[i];
    }

    return hash;
}

static struct api_interface *_api_get(enum API_HTTP_METHOD method, const char *url, size_t url_len)
{
    uint32_t hash = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct list_head *head = NULL;
    struct api_interface *iface = NULL;

    hash = _api_hash(method, url, url_len);
    head = &s_api_head[API_IDX(hash)];

    list_for_each_prev_safe(pos, next, head) {
        iface = (struct api_interface *) pos;
        if (iface->hash == hash
            && iface->method == method
            && url_len == iface->url_len
            && strncmp(iface->url, url, url_len) == 0) {
            return iface;
        }
    }

    return NULL;
}

const struct api_interface **api_get_all_post(int *nums)
{
    assert(nums != 0);

    *nums = s_api_interface_idx;
    return (const struct api_interface **)s_api_interface;
}

struct api_interface *api_get(const char *method, size_t method_len, const char *url, size_t url_len)
{
    enum API_HTTP_METHOD m = 0;
    struct api_interface *iface = NULL;

    if (method == NULL || url == NULL) {
        LOG_ERROR("Inner exception.");
        return NULL;
    }

    switch (method[0]) {
    case 'G':
        if (method_len == 3 && strncmp(method, "GET", 3) == 0) {
            m = API_HTTP_GET;
            break;
        }

        goto _default;
    case 'P':
        if (method_len == 4 && strncmp(method, "POST", 4) == 0) {
            m = API_HTTP_POST;
            break;
        } else if (method_len == 5 && strncmp(method, "PATCH", 5) == 0) {
            m = API_HTTP_PATCH;
            break;
        }

        goto _default;
    case 'D':
        if (method_len == 6 && strncmp(method, "DELETE", 6) == 0) {
            m = API_HTTP_DELETE;
            break;
        }

        goto _default;
    default:
    _default:
        LOG_ERROR("Not support method(%.*s).", method_len, method);
        return NULL;
    }

    iface = _api_get(m, url, url_len);
    if (iface == NULL) {
        LOG_ERROR("method(%s), url: %.*s not register.", s_api_method_string[m], url_len, url);
        return NULL;
    }

    return iface;
}

void api_register(enum API_HTTP_METHOD method, const char *container, const char *url, api_cb_t cb)
{
    uint32_t idx = 0;
    size_t url_len = 0;
    struct list_head *head = NULL;
    struct api_interface *iface = NULL;

    if (container == NULL || url == NULL || cb == NULL) {
        const char *tmp1 = container != NULL ? container : "null";
        const char *tmp2 = url != NULL ? url : "null";
        LOG_ERROR("container: %s, url: %s, cb: %p",  tmp1, tmp2, cb);
        exit(EXIT_FAILURE);
    }

    url_len = strlen(url);
    iface = _api_get(method, url, url_len);
    if (iface != NULL) {
        LOG_ERROR("method: %s, url: %s exists", s_api_method_string[method], url);
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
    iface->url_len = url_len;
    iface->callback = cb;
    INIT_LIST_HEAD(&iface->node);

    iface->hash = _api_hash(method, url, url_len);
    idx = API_IDX(iface->hash);
    head = &s_api_head[idx];

    list_add(&iface->node, head);
    if (method == API_HTTP_POST) {
        s_api_interface[s_api_interface_idx++] = iface;
    }

    LOG_INFO("(%s, %s, %s) SUCCESS", container, s_api_method_string[method], url);
}

void api_param_set_error(void *param, int code, const char *msg)
{
    char *out = ((struct api_param *) param)->output;
    snprintf(out, API_OUTPUT_LEN, API_RETURN_ERROR, code, msg);
}

int api_param_set_success(void *param, void *data)
{
    char *str = NULL;
    struct api_param *ap = param;

    if (data != NULL) {
        str = json_dumps(data, 0);
        if (str == NULL) {
            LOG_ERROR("OOM");
            return -1;
        }

        snprintf(ap->output, API_OUTPUT_LEN, API_RETURN_SUCCESS, str);
        free(str);
    } else {
        snprintf(ap->output, API_OUTPUT_LEN, API_RETURN_SUCCESS, "\"\"");
    }

    return 0;
}

int api_json_set(void *obj, const char *key, void *value, int type)
{
    int ret = 0;

    switch (type) {
    case JSON_OBJECT:
    case JSON_ARRAY:
        ret = json_object_set(obj, key, value);
        if (ret != 0) {
            LOG_ERROR("OOM");
            return -1;
        }

        break;
    case JSON_STRING: {
        json_t *str = json_string(value);
        if (str == NULL) {
            LOG_ERROR("OOM.");
            return -1;
        }

        ret = json_object_set(obj, key, str);
        if (ret != 0) {
            LOG_ERROR("OOM");
            json_decref(str);
            return -1;
        }

        break;
    }
    case JSON_INTEGER: {
        json_t *ivalue = json_integer(*(json_int_t *)value);
        if (ivalue == NULL) {
            LOG_ERROR("OOM.");
            return -1;
        }

        ret = json_object_set(obj, key, ivalue);
        if (ret != 0) {
            LOG_ERROR("OOM");
            json_decref(ivalue);
            return -1;
        }

        break;
    }
    case JSON_REAL: {
        json_t *real = json_real(*(double *)value);
        if (real == NULL) {
            LOG_ERROR("OOM.");
            return -1;
        }

        ret = json_object_set(obj, key, real);
        if (ret != 0) {
            LOG_ERROR("OOM");
            json_decref(real);
            return -1;
        }

        break;
    }
    case JSON_TRUE: {
        json_t *b = json_true();
        if (b == NULL) {
            LOG_ERROR("OOM.");
            return -1;
        }

        ret = json_object_set(obj, key, b);
        if (ret != 0) {
            LOG_ERROR("OOM");
            json_decref(b);
            return -1;
        }

        break;
    }
    case JSON_FALSE: {
        json_t *b = json_false();
        if (b == NULL) {
            LOG_ERROR("OOM.");
            return -1;
        }

        ret = json_object_set(obj, key, b);
        if (ret != 0) {
            LOG_ERROR("OOM");
            json_decref(b);
            return -1;
        }

        break;
    }
    default: {
        json_t *n = json_null();
        if (n == NULL) {
            LOG_ERROR("OOM.");
            return -1;
        }

        ret = json_object_set(obj, key, n);
        if (ret != 0) {
            LOG_ERROR("OOM");
            json_decref(n);
            return -1;
        }

        break;
    }
    }

    return ret;
}