/************************************************
 * filename: api_arp.c
 * function:
 * description:
 ***********************************************/

#include <jansson.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#include "log.h"
#include "api_inner.h"

API_POST(/v1/network/arp, arp)
{
    return api_success(NULL);
}

API_PUT(/v1/network/arp, arp)
{
    return api_success(NULL);
}

API_DELETE(/v1/network/arp, arp)
{
    return api_success(NULL);
}

API_GET(/v1/network/arp, arp)
{
    void *obj = NULL;
    const char *path = "/v1:arp";

    if (*url == 0) {
        obj = api_db_query(path);
        if (obj == NULL) {
            return api_failure(API_ERRCODE_INNER, "Internal server error");
        }

        return api_success(obj);
    }

    return api_success(NULL);
}