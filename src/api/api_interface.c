/*****************************************************************************
 * filename: api_interface.c
 * function:
 * description:
 ****************************************************************************/

#include <arpa/inet.h>
#include <linux/netfilter.h>

#include <jansson.h>
#include <sysrepo.h>

#include "l2.h"
#include "rcu.h"
#include "type.h"
#include "api_inner.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_common.h"

struct dpdk_interface_st {
    const char *name;
    int af;
    uint16_t port;
    union nf_inet_addr addr;
    struct dpdk_mac mac;
};

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

static void _api_interface_iface_free(void *arg)
{
    struct iface *iface = arg;

    if (iface != NULL) {
        dpdk_free(iface);
    }
}

static int _api_interface_send_garp(struct root *root, struct dpdk_interface_st dpdk_iface[], int nums)
{
    int ret = 0;
    static int id = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_interface_st *one = NULL;

    for (int i = 0; i < nums; i++) {
        one = &dpdk_iface[i];
        id = (id + 1) % root->hw_info.cpu_count;

        ret = dpdk_pktmbuf_pop(root->dpdk_thread[id]->pktmbuf, &mbuf, 1);
        if (ret != 0) {
            LOG_ERROR("Failure dpdk_pktmbuf_pop: %s", strerror(-ret));
            return -1;
        }

        ret = l2_garp_gen(mbuf, one->addr.ip, &one->mac);
        if (ret != 0) {
            dpdk_pktmbuf_push(&mbuf, 1);
            return -1;
        }

        mbuf->port = one->port;
        dpdk_ring_mp_push(root->dpdk_thread[id]->notice_ring, (void *const *)&mbuf, 1);
    }

    return 0;
}

API_POST(/v1/network/interface, interface)
{
    int ret = 0;
    int count = 0;
    json_t *array = NULL;
    struct root *root = cfg;
    struct iface *old = NULL;
    struct iface *one = NULL;
    struct numa_config *nc = NULL;
    void *update[CPU_MAX] = {NULL};
    void **ifaces[CPU_MAX] = {NULL};
    struct dpdk_interface_st dpdk_ifaces[DPDK_ETHPORT_MAX] = {0};

    array = json_object_get(json_object_get(json, "v1:interface"), "entrys");
    count = json_array_size(array);
    if (count > DPDK_ETHPORT_MAX) {
        LOG_ERROR("Too many network interfaces: %d", count);
        return api_failure(API_ERRCODE_PORT_TOO_MANY, "port too many");
    }

    for (int i = 0; i < count; i++) {
        json_t *obj = NULL;
        const char *ip = NULL;
        const char *iface_name = NULL;
        struct dpdk_interface_st *dpdk_iface = &dpdk_ifaces[i];

        obj = json_array_get(array, i);
        if (obj == NULL) {
            LOG_ERROR("Unknown error");
            goto _quit;
        }

        iface_name = json_string_value(json_object_get(obj, "name"));
        ip = json_string_value(json_object_get(obj, "ip"));

        dpdk_iface->name = iface_name;
        if (inet_pton(AF_INET, ip, &dpdk_iface->addr) != 1) { // todo ipv4
            LOG_ERROR("Failure inet_pton: %s", ip);
            goto _quit;
        }

        dpdk_iface->port = dpdk_port_by_name_get(iface_name);
        if (dpdk_iface->port == USHRT_MAX) {
            LOG_ERROR("Not exists(%s)", iface_name);
            goto _quit;
        }

        ret = dpdk_port_startup(dpdk_iface->port);
        if (ret != 0) {
            goto _quit;
        }

        ret = dpdk_port_mac(dpdk_iface->port, &dpdk_iface->mac);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) dpdk_port_mac: %s", dpdk_iface->port, strerror(-ret));
            goto _quit;
        }
    }

    old = root->dpdk_thread[0]->nc->iface;
    if (count + old->nums > DPDK_ETHPORT_MAX) {
        LOG_ERROR("Too many network interfaces(%d, %d)", old->nums, count);
        return api_failure(API_ERRCODE_PORT_TOO_MANY, "Port too many");
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        one = dpdk_malloc_numa((old->nums + count) * sizeof(uint16_t) + sizeof(struct iface), root->dpdk_thread[i]->hw_numa_id);
        if (one == NULL) {
            LOG_ERROR("OOM.");
            goto _quit;
        }

        dpdk_memcpy(one->port, old->port, sizeof(*old) + old->nums * sizeof(old->port[0]));
        for (int i = 0; i < count; i++) {
            one->port[one->nums++] = dpdk_ifaces[i].port;
        }

        update[i] = one;
    }

    ret = _api_interface_send_garp(root, dpdk_ifaces, count);
    if (ret != 0) {
        goto _quit;
    }

    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        ifaces[i] = (void **)&root->dpdk_thread[i]->nc->iface;
    }

    api_config_update(root, ifaces, update, _api_interface_iface_free);
    return api_success(NULL);

_quit:
    for (int i = 0; i < root->hw_info.cpu_count; i++) {
        if (update[i] == NULL) {
            break;
        }

        dpdk_free(update[i]);
    }
    return api_failure(API_ERRCODE_INNER, "Server inner error");
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