/************************************************
 * filename: api_common.h
 * function:
 * description:
 ***********************************************/

#include <stdio.h>

#include <jansson.h>
#include <mongoose.h>
#include <openssl/evp.h>

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "dpdk_rcu.h"
#include "api_inner.h"

#define ACCOUNT_SALT "M8#zY1$pQr!T2xVa"

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

int api_account_desensitize(unsigned char *dst, size_t max, const char *passwd, size_t len)
{
    int rc = 0;
    size_t nbytes = 0;
    unsigned char binary[64] = "";
    const char *salt = ACCOUNT_SALT;
    size_t salt_len = sizeof(ACCOUNT_SALT) - 1;

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
    int ret = 0;
    void *obj = NULL;
    json_error_t error = {0};

    RUNTIME_ASSERT(json != NULL);

    obj = json_loads(string, 0, &error);
    if (obj == NULL) {
        LOG_ERROR("line: %d, column: %d, position: %d, source: %s, text: %s",
                   error.line, error.column, error.position, error.source, error.text);
        return -1;
    }

    *json = obj;
    return 0;
}

int api_json_to_string(void *json, char **string)
{
    char *tmp = NULL;

    RUNTIME_ASSERT(string != NULL);

    tmp = json_dumps(json, 0);
    if (tmp == NULL) {
        LOG_ERROR("OOM.");
        return -1;
    }

    *string = tmp;
    return 0;
}

int api_json_add_string(void *json, const char *name, const char *value)
{
    int ret = 0;
    void *obj_value = NULL;

    obj_value = json_string(value);
    if (obj_value == NULL) {
        LOG_ERROR("OOM");
        return -1;
    }

    ret = json_object_set_new(json, name, obj_value);
    if (ret != 0) {
        LOG_ERROR("OOM");
        json_decref(obj_value);
        return -1;
    }

    return 0;
}

int api_json_add_integer(void *json, const char *name, json_int_t value)
{
    int ret = 0;
    void *obj_value = NULL;

    obj_value = json_integer(value);
    if (obj_value == NULL) {
        LOG_ERROR("OOM.");
        return -1;
    }

    ret = json_object_set_new(json, name, obj_value);
    if (ret != 0) {
        LOG_ERROR("OOM.");
        json_decref(obj_value);
        return -1;
    }

    return 0;
}

void api_config_update(void *cfg, void **position[], void *update[], void (*free_post)(void *[], int))
{
    struct root *root = cfg;
    struct dataplane *dp = NULL;
    void *old[CPU_MAX] = {NULL};
    void *numa[NUMA_MAX] = {NULL};
    void *numa_rcu[NUMA_MAX] = {NULL};
    int cpu_count = root->hw_info.cpu_count;

    for (int i = 0; i < cpu_count; i++) {
        old[i] = *position[i];
    }

    for (int i = 0; i < cpu_count; i++) {
        rcu_assign_pointer(position[i], update[i]);
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        dp = root->dpdk_thread[i];
        numa_rcu[dp->numa_id] = dp->rcu;
        numa[dp->numa_id] = old[dp->numa_id];
    }

    for (int i = 0; i < root->hw_info.numa_count; i++) {
        if (numa_rcu[i] != NULL) {
            dpdk_rcu_synchronize(numa_rcu[i]);
        }
    }

    free_post(numa, root->hw_info.numa_count);
}