/************************************************
 * filename: api_account.c
 * function:
 * description:
 ***********************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sysrepo.h>
#include <jansson.h>

#include "log.h"
#include "api_inner.h"

static void *api_account_post_update(const char *url, void *json, void *sess)
{
    int i = 0;
    int ret = 0;
    void *obj = NULL;
    void *value = NULL;
    void *subobj = NULL;
    void *user_obj = NULL;
    size_t passwd_len = 0;
    const char *passwd = NULL;

    char path[256] = "";
    unsigned char dst[128] = "";

    obj = json_object_get(json, "v1:account");
    user_obj = json_object_get(obj, "users");
    json_array_foreach(user_obj, i, subobj) {
        value = json_object_get(subobj, "password");
        passwd = json_string_value(value);
        passwd_len = json_string_length(value);

        ret = api_account_desensitize(dst, sizeof(dst), passwd, passwd_len);
        if (ret != 0) {
            LOG_ERROR("account desensitize failure.");
            return api_failure(API_ERRCODE_ACCOUNT, "Account exception");
        }

        const char *username = json_string_value(json_object_get(subobj, "username"));
        snprintf(path, sizeof(path), "/v1:account/users[username='%s']/password", username);

        ret = sr_set_item_str(sess, path, dst, NULL, SR_EDIT_DEFAULT);
        if (ret != 0) {
            LOG_ERROR("sr_set_item_str failure: %s", sr_strerror(ret));
            return api_failure(API_ERRCODE_ACCOUNT, "Account exception");
        }
    }

    return api_success(NULL);
}

static void *api_account_put_update(const char *url, void *json, void *sess)
{
    return api_success(NULL);
}

API_DELETE(/v1/system/account, account)
{
    return api_success(NULL);
}

API_GET(/v1/system/account, account)
{
    int i = 0;
    void *obj = NULL;
    void *subobj = NULL;
    void *users_obj = NULL;
    void *username_obj = NULL;

    obj = api_db_query(sess, "v1", "account");
    if (obj == NULL) {
        return api_failure(API_ERRCODE_INNER, "Internal server error");
    }

    subobj = json_object_get(obj, "v1:account");
    users_obj = json_object_get(subobj, "users");
    json_array_foreach(users_obj, i, username_obj) {
        json_object_set_new(username_obj, "password", json_string("********"));
    }

    return api_success(obj);
}

API_POST_NO_LOAD_REGISTER(/v1/system/account, account, api_account_post_update, NULL, NULL);
API_PUT_REGISTER(/v1/system/account, account, api_account_put_update, NULL, NULL)