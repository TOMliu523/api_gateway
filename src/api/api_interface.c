/************************************************
 * filename: api_interface.c
 * function:
 * description:
 ***********************************************/

#include <stdint.h>

#include <jansson.h>
#include <sysrepo.h>

#include "log.h"
#include "macro.h"
#include "api_inner.h"
#include "dpdk_port.h"

static void *_api_iface_obj_gen(const char *name, sr_val_t *val)
{
    int ret = 0;
    void *obj = NULL;

    obj = json_object();
    if (obj == NULL) {
        LOG_ERROR("OOM");
        return NULL;
    }

    ret = api_json_add_string(obj, "name", name);
    if (ret != 0) {
        goto _quit;
    }

    if (val != NULL) {
        ret = api_json_add_string(obj, "ip", val->data.string_val);
        if (ret != 0) {
            goto _quit;
        }
    }

    return obj;

_quit:
    if (obj != NULL) {
        json_decref(obj);
    }

    return NULL;
}

API_POST(/v1/network/interface, interface)
{
    return api_success(NULL);
}

API_PUT(/v1/network/interface, interface)
{
    return api_success(NULL);
}

API_DELETE(/v1/network/interface, interface)
{
    return api_success(NULL);
}

API_GET(/v1/network/interface, interface)
{
    int ret = 0;
    void *obj = NULL;
    void *array = NULL;
    sr_val_t *val = NULL;
    char path[CACHE_LINE] = "";
    const struct port_name *port_name = NULL;
    const struct port_name_entry *one = NULL;
    const char *format = "/v1:interface/entry[name='%s']/ip";

    array = json_array();
    if (array == NULL) {
        LOG_ERROR("OOM.");
        return api_failure(API_ERRCODE_INNER, "Server inner error");
    }

    port_name = dpdk_port_name_get();
    for (int i = 0; i < port_name->count; i++) {
        one = &port_name->entrys[i];

        snprintf(path, sizeof(path), format, one->name);
        ret = sr_get_item(sess, path, 0, &val);
        if (ret != 0 && ret != SR_ERR_NOT_FOUND) {
            LOG_ERROR("Failure path(%s) sr_get_item:%s", path, strerror(-ret));
            goto _quit;
        }

        obj = _api_iface_obj_gen(one->name, val);
        if (obj == NULL) {
            goto _quit;
        }

        ret = json_array_append_new(array, obj);
        if (ret != 0) {
            LOG_ERROR("OOM.");
            json_decref(obj);
            goto _quit;
        }
    }

    return api_success(array);

_quit:
    if (array != NULL) {
        json_decref(array);
    }

    return api_failure(API_ERRCODE_INNER, "Server inner error");
}