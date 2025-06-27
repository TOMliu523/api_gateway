/************************************************
 * filename: api_account.c
 * function:
 * description:
 ***********************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sysrepo.h>
#include <jansson.h>

#include "log.h"
#include "errcode.h"
#include "api_inner.h"

static void *_api_account(void *cfg, const char *url, void *json, void *sess, bool update_id)
{
    int i = 0;
    int ret = 0;
    void *obj = NULL;
    void *value = NULL;
    void *subobj = NULL;
    void *user_obj = NULL;
    size_t passwd_len = 0;
    const char *passwd = NULL;
    sr_val_t *modify_id = NULL;

    char path[256] = "";
    unsigned char dst[128] = "";

    obj = json_object_get(json, "v1:account");
    user_obj = json_object_get(obj, "users");
    json_array_foreach(user_obj, i, subobj) {
        value = json_object_get(subobj, "password");
        passwd = json_string_value(value);
        passwd_len = json_string_length(value);

        ret = api_account_desensitize(dst, sizeof(dst), passwd, passwd_len);
        if (ret < 0) {
            LOG_ERROR("account desensitize failure.");
            return api_fail(ERRCODE_ACCOUNT);
        }

        const char *username = json_string_value(json_object_get(subobj, "username"));
        snprintf(path, sizeof(path), "/v1:account/users[username='%s']/password", username);

        ret = sr_set_item_str(sess, path, dst, NULL, SR_EDIT_DEFAULT);
        if (ret != 0) {
            LOG_ERROR("sr_set_item_str failure: %s", sr_strerror(ret));
            return api_fail(ERRCODE_ACCOUNT);
        }

        if (update_id) {
            continue;
        }

        snprintf(path, sizeof(path), "/v1:account/users[username='%s']/modify_id", username);
        ret = sr_get_item(sess, path, 0, &modify_id);
        if (ret != 0) {
            LOG_ERROR("sr_get_item_str failure: %s", sr_strerror(ret));
            return api_fail(ERRCODE_ACCOUNT);
        }

        modify_id->data.uint64_val += 1;
        ret = sr_set_item(sess, path, modify_id, SR_EDIT_DEFAULT);
        if (ret != 0) {
            LOG_ERROR("sr_set_item_str modify_id failure: %s", sr_strerror(ret));
            return api_fail(ERRCODE_ACCOUNT);
        }
    }

    return api_succ(NULL);
}

static void *api_account_post_update(void *cfg, const char *url, void *json, void *sess)
{
    return _api_account(cfg, url, json, sess, true);
}

static void *api_account_put_update(void *cfg, const char *url, void *json, void *sess)
{
    return _api_account(cfg, url, json, sess, false);
}

API_DELETE(/v1/system/account, account)
{
    return api_succ(NULL);
}

API_GET(/v1/system/account, account)
{
    int i = 0;
    int ret = 0;
    void *obj = NULL;
    void *subobj = NULL;
    void *users_obj = NULL;
    void *username_obj = NULL;
    const char *path = "/v1:account";

    ret = api_db_query(path, &obj);
    if (ret != 0) {
        return api_fail(ERRCODE_INNER);
    }

    if (obj != NULL) {
        subobj = json_object_get(obj, "v1:account");
        users_obj = json_object_get(subobj, "users");
        json_array_foreach(users_obj, i, username_obj) {
            json_object_set_new(username_obj, "password", json_string("********"));
        }
    }

    return api_succ(obj);
}

API_POST_NO_LOAD_REGISTER(/v1/system/account, account, api_account_post_update, NULL, NULL);
API_PUT_REGISTER(/v1/system/account, account, api_account_put_update, NULL, NULL)