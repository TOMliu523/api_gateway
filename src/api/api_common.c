/************************************************
 * filename: api_common.h
 * function:
 * description:
 ***********************************************/

#include <stdio.h>

#include <jansson.h>
#include <mongoose.h>
#include <openssl/evp.h>

#include "api_inner.h"

#define ACCOUNT_SALT "M8#zY1$pQr!T2xVa"

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

    return 0;
}