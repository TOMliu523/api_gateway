/*****************************************************************************
 * filename: api_port.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>

#include "log.h"
#include "api.h"
#include "dpdk_port.h"

API_GET(port, /v1/port)
{
    int ret = 0;
    void *data = NULL;
    const struct port_info *info = NULL;

    info = dpdk_port_info_get();
    if (info == NULL) {
        LOG_INFO("PORT info failure.");
        api_param_set_error(param, API_STATUS_INTERNAL, "PORT info failure");
        return -1;
    }

    data = json_array();
    if (data == NULL) {
        goto _quit;
    }

    for (int i = 0; i < info->count; i++) {
        void *json = NULL;
        json_int_t jint = 0;
        const struct port_info_entry *one = &info->info[i];

        json = json_object();
        if (json == NULL) {
            goto _quit;
        }

        jint = (json_int_t)one->port;
        ret = api_json_set(json, "id", &jint, JSON_INTEGER);
        if (ret != 0) {
            json_decref(json);
            goto _quit;
        }

        ret = api_json_set(json, "name", (void *)one->name, JSON_STRING);
        if (ret != 0) {
            json_decref(json);
            goto _quit;
        }

        ret = api_json_set(json, "pci", (void *)one->pci, JSON_STRING);
        if (ret != 0) {
            json_decref(json);
            goto _quit;
        }

        ret = json_array_append_new(data, json);
        if (ret != 0) {
            json_decref(json);
            goto _quit;
        }
    }

    ret = api_param_set_success(param, data);
    if (ret != 0) {
        goto _quit;
    }

    json_decref(data);
    return 0;

_quit:
    LOG_ERROR("OOM");
    json_decref(data);
    api_param_set_error(param, API_STATUS_INTERNAL, "PORT info failure");
    return -1;
}