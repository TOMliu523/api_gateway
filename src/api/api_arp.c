/************************************************
 * filename: api_arp.c
 * function:
 * description:
 ***********************************************/

#include <jansson.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "errcode.h"
#include "api_inner.h"

API_POST(/v1/network/arp, arp)
{
    int ret = 0;
    char *str = NULL;

    ret = api_json_to_string(json, &str);
    if (ret < 0) {
        return api_fail(ERRCODE_INNER);
    }

    LOG_DEBUG("Load arp config: %s", str);
    free(str);

    return api_succ(NULL);
}

API_PUT(/v1/network/arp, arp)
{
    return api_succ(NULL);
}

API_DEL(/v1/network/arp, arp)
{
    return api_succ(NULL);
}

API_GET(/v1/network/arp, arp)
{
    int ret = 0;
    void *obj = NULL;
    const char *path = "/v1:arp";

    if (*url == 0) {
        ret = api_db_query(path, &obj);
        if (ret != 0) {
            return api_fail(ERRCODE_INNER);
        }
    }

    return api_succ(NULL);
}