/************************************************
 * filename: api_login.c
 * function:
 * description:
 ***********************************************/

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <jansson.h>
#include <mongoose.h>

#include "api_inner.h"
#include "macro.h"

#define API_AUTH_EXP (20 * 60)
#define API_AUTH_BEARER "Bearer "
#define API_AUTH_HEADER "{\"alg\":\"HS256\",\"typ\":\"JWT\"}"
#define API_AUTH_PAYLOAD "{\"iss\":\"TML\",\"iat\":%lu,\"exp\":%lu,\"jti\":%lu,\"usr\":\"%s\",\"roles\":\"%s\",\"rev\":%u}"

#define API_USER_PATH "/v1:account/users[username='%s']"

struct user_login {
    char username[32];
    char password[192];
    size_t password_len;
};

struct db_login {
    char username[32];
    char encrypt_data[192];
    size_t encrypt_len;
    char role_type[64];
    uint32_t modify_id;
};

static int _api_login_user(struct user_login *user, void *arg)
{
    json_t *json = NULL;
    json_t *username = NULL;
    json_t *password = NULL;
    json_error_t error = {0};
    struct mg_http_message *msg = arg;

    json = json_loadb(msg->body.buf, msg->body.len, 0, &error);
    if (json == NULL) {
        LOG_ERROR("line: %d, column: %d, position: %d, source: %s, text: %s",
            error.line, error.column, error.position, error.source, error.text);
        return -1;
    }

    username = json_object_get(json, "username");
    if (username == NULL) {
        LOG_ERROR("Input format error.");
        json_decref(json);
        return -1;
    }

    password = json_object_get(json, "password");
    if (password == NULL) {
        LOG_ERROR("Input format error.");
        json_decref(json);
        return -1;
    }

    strcpy(user->username, json_string_value(username));
    strcpy(user->password, json_string_value(password));
    user->password_len = json_string_length(password);

    json_decref(json);
    return 0;
}

static void _api_db_get(struct db_login *db, void *json)
{
    void *tmp = NULL;
    json_t *users = NULL;

    users = json_array_get(json_object_get(json, "v1:users"), 0);
    strcpy(db->username, json_string_value(json_object_get(users, "username")));
    tmp = json_object_get(users, "password");
    strcpy(db->encrypt_data, json_string_value(tmp));
    db->encrypt_len = json_string_length(tmp);
    strcpy(db->role_type, json_string_value(json_object_get(users, "role_type")));
    db->modify_id = json_integer_value(json_object_get(users, "modify_id"));
}

static int _api_check_user(struct db_login *db, struct user_login *user)
{
    int ret = 0;
    json_t *json = NULL;
    char path[128] = "";
    char data[192] = "";

    snprintf(path, sizeof(path), API_USER_PATH, user->username);
    ret = api_db_query(path, (void **)&json);
    if (ret != 0) {
        return -1;
    }

    if (json == NULL) {
        if (strcmp(user->username, "admin") != 0 || strcmp(user->password, "Tmlake@2025") != 0) {
            LOG_ERROR("username or password error");
            ret = -1;
            goto _quit;
        } else {
            ret = 0;
            goto _quit;
        }
    }

    _api_db_get(db, json);

    ret = api_account_desensitize(data, sizeof(data), user->password, user->password_len);
    if (ret < 0) {
        goto _quit;
    }

    if (db->encrypt_len != ret || strcmp(db->encrypt_data, data) != 0) {
        LOG_ERROR("username or password error.");
        return -1;
    }

    ret = 0;

_quit:
    json_decref(json);
    return ret;
}

static struct mg_str *_api_login_auth_header(struct mg_http_message *msg)
{
    struct mg_str *value = NULL;
    struct mg_http_header *header = NULL;

    value = mg_http_get_header(msg, API_AUTH);
    if (value != NULL) {
        return value;
    }

    for (int i = 0; i < MG_MAX_HTTP_HEADERS; i++) {
        header = &msg->headers[i];
        if (header->name.buf == NULL) {
            header->name.buf = API_AUTH;
            header->name.len = sizeof(API_AUTH) - 1;
            return &header->value;
        }
    }

    return NULL;
}

static int _api_jwt(struct mg_http_message *msg, const struct db_login *db)
{
    int ret = 0;
    size_t t = 0;
    int nbytes = 0;
    int bearer_len = 0;
    size_t header_len = 0;
    size_t payload_len = 0;
    size_t sign_len = 0;
    char buf[256] = "";
    struct mg_str *header = NULL;

    static __thread uint64_t id = 0;
    static __thread char auth[1024] = API_AUTH_BEARER;

    id += 1;
    bearer_len = nbytes = sizeof(API_AUTH_BEARER) - 1;
    t = time(NULL);

    header_len = mg_base64_encode(API_AUTH_HEADER, sizeof(API_AUTH_HEADER), auth + nbytes, sizeof(auth) - nbytes);
    nbytes += header_len;
    auth[nbytes++] = '.';

    ret = snprintf(buf, sizeof(buf), API_AUTH_PAYLOAD, t, t + API_AUTH_EXP, id, db->username, db->role_type, db->modify_id);
    payload_len = mg_base64_encode(buf, ret, auth + nbytes, sizeof(auth) - nbytes);
    nbytes += payload_len;
    auth[nbytes++] = '.';

    ret = api_account_desensitize(auth + nbytes, sizeof(auth) - nbytes, auth + bearer_len, nbytes - bearer_len);
    if (ret < 0) {
        return -1;
    }

    header = _api_login_auth_header(msg);
    if (header == NULL) {
        return -1;
    }

    header->buf = auth;
    header->len = nbytes + ret;

    return 0;
}

static int _api_login_db_get(struct db_login *db, const char *username)
{
    int ret = 0;
    json_t *json = NULL;
    char path[128] = "";

    snprintf(path, sizeof(path), API_USER_PATH, username);
    ret = api_db_query(path, (void **)&json);
    if (ret != 0) {
        return -1;
    }

    if (json == NULL) {
        if (strcmp(username, "admin") != 0) {
            LOG_ERROR("user(%s) not exists", username);
            return -1;
        }

        strcpy(db->username, username);
        strcpy(db->role_type, "SYSTEM_ADMIN");
        db->modify_id = 0;
    } else {
        _api_db_get(db, json);
        json_decref(json);
    }

    return 0;
}

static enum API_ERRCODE _api_login_exp_and_flush(void *arg, const char *base, size_t len)
{
    size_t nbytes = 0;
    char data[192] = "";
    json_t *json = NULL;
    json_error_t error = {0};
    const char *usr = NULL;
    struct db_login db = {0};
    uint32_t modify_id = 0;
    struct mg_http_message *msg = arg;
    enum API_ERRCODE ret = API_ERRCODE_SUCCESS;

    size_t cur = 0;
    json_int_t exp = 0;
    json_int_t iat = 0;

    nbytes = mg_base64_decode(base, len, data, sizeof(data));
    json = json_loadb(data, nbytes, 0, &error);
    if (json == NULL) {
        LOG_ERROR("line: %d, column: %d, position: %d, source: %s, text: %s",
                  error.line, error.column, error.position, error.source, error.text);
        return API_ERRCODE_AUTH;
    }

    cur = time(NULL);
    usr = json_string_value(json_object_get(json, "usr"));
    exp = json_integer_value(json_object_get(json, "exp"));

    if (exp <= cur) {
        LOG_WARN("username(%s) login expire", usr);
        ret = API_ERRCODE_AUTH;
        goto _quit;
    }

    if (_api_login_db_get(&db, usr) != 0) {
        ret = API_ERRCODE_AUTH;
        goto _quit;
    }

    if (msg->uri.len == 18 && strncmp(msg->uri.buf, "/v1/system/account", 18) == 0) {
        if (strcmp(db.role_type, "SYSTEM_ADMIN") != 0 && strcmp(db.role_type, "SYSTEM_ROOT") != 0) {
            ret = API_ERRCODE_FORBIDDEN;
            goto _quit;
        }
    }

    iat = json_integer_value(json_object_get(json, "iat"));
    if (cur - iat >= API_AUTH_EXP / 2) {
        ret = _api_jwt(arg, &db);
        if (ret != 0) {
            ret = API_ERRCODE_AUTH;
            goto _quit;
        }
    } else {
        modify_id = (uint32_t)json_integer_value(json_object_get(json, "rev"));
        if (modify_id != db.modify_id) {
            ret = API_ERRCODE_AUTH;
            goto _quit;
        }
    }

_quit:
    json_decref(json);
    return ret;
}

enum API_ERRCODE api_login(void *arg)
{
    int ret = 0;
    char data[192] = "";
    struct db_login db = {
        .username = "admin",
        .encrypt_data = "Tmlake@2025",
        .encrypt_len = 11,
        .role_type = "SYSTEM_ADMIN",
        .modify_id = 0,
    };
    struct user_login user = {0};

    ret = _api_login_user(&user, arg);
    if (ret != 0) {
        return API_ERRCODE_USER_PWD;
    }

    ret = _api_check_user(&db, &user);
    if (ret != 0) {
        return API_ERRCODE_USER_PWD;
    }

    ret = _api_jwt(arg, &db);
    if (ret != 0) {
        return API_ERRCODE_USER_PWD;
    }

    return API_ERRCODE_SUCCESS;
}

enum API_ERRCODE api_refresh_login(void *arg)
{
    int ret = 0;
    size_t len = 0;
    char buf[1024] = "";
    const char *tmp = NULL;
    const char *sign = NULL;
    const char *header = NULL;
    const char *first = NULL;
    const char *second = NULL;
    struct mg_str *auth = NULL;
    size_t bearer_len = sizeof(API_AUTH_BEARER) - 1;

    auth = mg_http_get_header(arg, API_AUTH);
    if (auth == NULL) {
        return API_ERRCODE_AUTH;
    }

    header = auth->buf + bearer_len;
    first = strchr(header, '.');
    if (first == NULL) {
        return API_ERRCODE_AUTH;
    }

    first += 1;
    second = strchr(first, '.');
    if (second == NULL) {
        return API_ERRCODE_AUTH;
    }

    second += 1;
    if (second - header >= auth->len - bearer_len) {
        return API_ERRCODE_AUTH;
    }

    ret = api_account_desensitize(buf, sizeof(buf), header, second - header);
    if (ret < 0) {
        return API_ERRCODE_AUTH;
    }

    if ((ret != auth->len - bearer_len - (second - header)) || strncmp(buf, second, ret) != 0) {
        return API_ERRCODE_AUTH;
    }

    ret = _api_login_exp_and_flush(arg, first, second - 1 - first);
    if (ret != API_ERRCODE_SUCCESS) {
        return ret;
    }

    return API_ERRCODE_SUCCESS;
}