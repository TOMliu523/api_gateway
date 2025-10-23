/*****************************************************************************
 * filename: api_status.c
 * function:
 * description:
 ****************************************************************************/

#include "type.h"
#include "api_inner.h"
#include "dpdk_core.h"

// test
API_GET(/v1/status/pktmbuf, pktmbuf)
{
    void *obj = NULL;
    void *array = NULL;
    struct root *root = cfg;
    unsigned int used_count = 0;
    unsigned int avail_count = 0;
    void *pool = root->dpdk_thread[0]->pktmbuf_pool;

    used_count = dpdk_mempool_used_count(pool);
    avail_count = dpdk_mempool_avail_count(pool);

    array = json_array();
    obj = json_object();
    api_json_add_long(obj, "avail_count", avail_count);
    api_json_add_long(obj, "used_count", used_count);
    json_array_append_new(array, obj);

    return api_succ(array);
}