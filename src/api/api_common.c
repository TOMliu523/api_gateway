/************************************************
 * filename: api_common.h
 * function:
 * description:
 ***********************************************/

#include <stdio.h>

#include <jansson.h>
#include <mongoose.h>
#include <arpa/inet.h>
#include <openssl/evp.h>

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "dpdk_rcu.h"
#include "api_inner.h"
#include "dpdk_common.h"

#define ACCOUNT_SALT "M8#zY1$pQr!T2xVa"

#define API_NODE_LEN_MAX 64

static const char *s_errcode_msg[] = {
#define ERRMSG(code, value, msg) [ERRCODE_##code] = msg,
ERRCODE_EXTEND(ERRMSG)
#undef ERRMSG
};

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

void *api_succ(void *obj)
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


void *api_fail_msg(int errcode, const char *errmsg)
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

void *api_fail(int errcode)
{
    return api_fail_msg(errcode, s_errcode_msg[errcode]);
}

int api_account_desensitize(char *dst, size_t max, const char *passwd, size_t len)
{
    int rc = 0;
    size_t nbytes = 0;
    unsigned char binary[64] = "";
    size_t salt_len = sizeof(ACCOUNT_SALT) - 1;
    const unsigned char *salt = (const unsigned char *)ACCOUNT_SALT;

    rc = PKCS5_PBKDF2_HMAC(passwd, len, salt, salt_len, 200000, EVP_sha256(), sizeof(binary), binary);
    if (rc != 1) {
        return -1;
    }

    nbytes = mg_base64_encode(binary, sizeof(binary), dst, max);
    dst[nbytes] = 0;

    return (int)nbytes;
}

int api_string_to_json(const char *string, void **json)
{
    void *obj = NULL;
    json_error_t error = {0};

    if (json == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    obj = json_loads(string, 0, &error);
    if (obj == NULL) {
        LOG_ERROR("line: %d, column: %d, position: %d, source: %s, text: %s",
                   error.line, error.column, error.position, error.source, error.text);
        return ERRCODE_INNER;
    }

    *json = obj;
    return 0;
}

int api_json_to_string(void *json, char **string)
{
    char *tmp = NULL;

    if (json == NULL || string == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    tmp = json_dumps(json, 0);
    if (tmp == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    *string = tmp;
    return 0;
}

int api_json_array_append(void *array, void *obj)
{
    int ret = 0;

    ret = json_array_append_new(array, obj);
    if (ret != 0) {
        LOG_ERROR("OOM");
        return ERRCODE_OOM;
    }

    return 0;
}

int api_json_object(void **obj)
{
    if (obj == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    *obj = json_object();
    if (*obj == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    return 0;
}

void api_json_free(void *ptr)
{
    if (ptr != NULL) {
        json_decref(ptr);
    }
}

int api_json_array(void **arr)
{
    if (arr == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_INNER;
    }

    *arr = json_array();
    if (*arr == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    return 0;
}

int api_json_add_object(void *json, const char *name, void *obj)
{
    int ret = 0;

    ret = json_object_set_new(json, name, obj);
    if (ret != 0) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    return 0;
}

int api_json_add_string(void *json, const char *name, const char *value)
{
    int ret = 0;
    void *obj_value = NULL;

    obj_value = json_string(value);
    if (obj_value == NULL) {
        LOG_ERROR("OOM");
        return ERRCODE_OOM;
    }

    ret = json_object_set_new(json, name, obj_value);
    if (ret != 0) {
        LOG_ERROR("OOM");
        json_decref(obj_value);
        return ERRCODE_OOM;
    }

    return 0;
}

int api_json_add_long(void *json, const char *name, long value)
{
    int ret = 0;
    void *obj_value = NULL;

    obj_value = json_integer((json_int_t)value);
    if (obj_value == NULL) {
        LOG_ERROR("OOM.");
        return ERRCODE_OOM;
    }

    ret = json_object_set_new(json, name, obj_value);
    if (ret != 0) {
        LOG_ERROR("OOM.");
        json_decref(obj_value);
        return ERRCODE_OOM;
    }

    return 0;
}

const char *api_json_get_string(void *obj, const char *name)
{
    void *subobj = NULL;
    const char *value = NULL;

    if (obj == NULL || name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return NULL;
    }

    subobj = json_object_get(obj, name);
    if (subobj == NULL) {
        LOG_ERROR("Json object '%s' not exists", name);
        return NULL;
    }

    value = json_string_value(subobj);
    if (value == NULL) {
        LOG_ERROR("Invalid parameter.");
        return NULL;
    }

    return value;
}

int api_json_get_long(uint64_t *value, void *obj, const char *name)
{
    void *subobj = NULL;

    if (value == NULL || obj == NULL || name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_OOM;
    }

    subobj = json_object_get(obj, name);
    if (subobj == NULL) {
        LOG_ERROR("Json object '%s' not exists");
        return ERRCODE_INVALID;
    }

    *value = (uint64_t)json_integer_value(subobj);
    return 0;
}

// Publish the updated configuration from the control plane to the global root
void api_thread_config_update(void *cfg, void **position[], void *update[], void (*free_cb)(void *))
{
    struct root *root = cfg;
    void *old[CPU_MAX] = {NULL};
    struct dataplane *dp = NULL;
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        old[i] = *position[i];
    }

    for (int i = 0; i < cpu_count; i++) {
        rcu_assign_pointer(position[i], update[i]);
        update[i] = NULL;
    }

    for (int i = 0; i < cpu_count; i++) {
        dp = root->dpdk_thread[i];
        dpdk_rcu_synchronize(dp->rcu);
    }

    if (free_cb != NULL) {
        for (int i = 0; i < cpu_count; i++) {
            free_cb(old[i]);
        }
    }
}

// need delete
void api_config_update(void **position[], void *update[], int count, void (*free_cb)(void *))
{
    void *old = NULL;

    for (int i = 0; i < count; i++) {
        old = *position[i];
        rcu_assign_pointer(position[i], update[i]);
        update[i] = old;
    }

    if (free_cb != NULL) {
        for (int i = 0; i < count; i++) {
            free_cb(update[i]);
        }
    }
}

int api_string_to_addr(int *af, union inet_addr *addr, const char *str)
{
    int ret = 0;

    if (af == NULL || addr == NULL || str == NULL) {
        LOG_ERROR("Invalid parameter");
        return ERRCODE_PARAMETER_INVALID;
    }

    if (strchr(str, ':') == 0) {
        *af = AF_INET;
    } else {
        *af = AF_INET6;
    }

    ret = inet_pton(AF_INET, str, addr);
    if (ret != 1) {
        LOG_ERROR("Invalid addr(%s)", addr);
        return ERRCODE_IP_INVALID;
    }

    return 0;
}

void api_free(void *ptr)
{
    if (ptr != NULL) {
        dpdk_free(ptr);
    }
}

void *api_malloc_numa(size_t size, int hw_numa)
{
    void *ptr = NULL;

    ptr = dpdk_malloc_numa(size, hw_numa);
    if (ptr == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(ptr, 0, size);
    return ptr;
}

void *api_malloc(size_t size)
{
    void *ptr = NULL;

    ptr = dpdk_malloc(size);
    if (ptr == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(ptr, 0, size);
    return ptr;
}

void *api_realloc(void *ptr, size_t size)
{
    void *new_ptr = NULL;

    new_ptr = dpdk_realloc(ptr, size);
    if (new_ptr == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    return new_ptr;
}

int api_v1_modify_list(void **array, size_t *count, void *json, const char *module_name, const char *list_name)
{
    int nbytes = 0;
    void *obj = NULL;
    void *ret = NULL;
    char buffer[BUFSIZ] = "";

    if (array == NULL || count == NULL || json == NULL || module_name == NULL || list_name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    if (strlen(module_name) >= API_NODE_LEN_MAX || strlen(list_name) >= API_NODE_LEN_MAX) {
        LOG_ERROR("Length %s or %s more than %d", module_name, list_name, API_NODE_LEN_MAX);
        return ERRCODE_NAME_TOO_LENGTH;
    }

    nbytes = snprintf(buffer, sizeof(buffer), "v1:%s", module_name);
    if (nbytes >= sizeof(buffer) || nbytes < 0) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    obj = json_object_get(json, buffer);
    if (obj == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    ret = json_object_get(obj, list_name);
    if (ret != 0) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *array = ret;
    *count = json_array_size(ret);

    return 0;
}

int api_v1_delete_list(void **array, size_t *count, void *json, const char *module_name, const char *list_name)
{
    size_t n = 0;
    int nbytes = 0;
    void *ret = NULL;
    char buffer[BUFSIZ] = "";

    if (array == NULL || count == NULL || json == NULL || module_name == NULL || list_name == NULL) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    if (strlen(module_name) >= API_NODE_LEN_MAX || strlen(list_name) >= API_NODE_LEN_MAX) {
        LOG_ERROR("Length %s or %s more than %d", module_name, list_name, API_NODE_LEN_MAX);
        return ERRCODE_NAME_TOO_LENGTH;
    }

    nbytes = snprintf(buffer, sizeof(buffer), "/v1:%s/%s", module_name, list_name);
    if (nbytes >= sizeof(buffer) || nbytes < 0) {
        LOG_ERROR("Invalid parameter(%s, %s).", module_name, list_name);
        return ERRCODE_PARAMETER_INVALID;
    }

    ret = json_object_get(json, buffer);
    if (ret == NULL) {
        LOG_ERROR("Invalid parameter(%s).", buffer);
        return ERRCODE_PARAMETER_INVALID;
    }

    n = json_array_size(ret);
    if (n == 0) {
        LOG_ERROR("Invalid parameter.");
        return ERRCODE_PARAMETER_INVALID;
    }

    *array = ret;
    *count = n;

    return 0;
}