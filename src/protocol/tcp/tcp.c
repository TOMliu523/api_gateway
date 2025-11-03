/*****************************************************************************
 * filename: tcp.c
 * function:
 * description:
 *****************************************************************************/

#include "tcp.h"
#include "type.h"
#include "dpdk_hash.h"
#include "dpdk_core.h"
#include "dpdk_limits.h"

static __thread void *s_tcp_conn_pool;
static __thread void *s_tcp_conn_hash;

void tcp_state_process(UNUSED struct dpdk_mbuf *mbuf, UNUSED struct tcp_conn *conn)
{
    tlv_drop->data[tlv_drop->count++] = mbuf;
}

void tcp_conn_lookup(struct tcp4_lookup_blk *blk)
{
    int count = blk->count;
    uint64_t *hit_mask = &blk->resutl;
    void **data = (void **)blk->conns;
    const void **keys = (const void **)blk->keys;

    dpdk_hash_lookup_bulk(s_tcp_conn_hash, keys, count, hit_mask, data);
}

int tcp_thread_create(void)
{
    int ret = 0;
    void *conn_hash = NULL;
    void *conn_pool = NULL;
    char name[CACHE_LINE] = "";

    snprintf(name, sizeof(name), "TCP_CONN_%u_%u", tlv_thread_id, (uint32_t)tlv_dp->off_time);
    conn_pool = dpdk_ring_ss_create(name, DP_TCP_CONN_MAX_PER_THREAD, tlv_hw_numa_id);
    if (UNLIKELY(conn_pool == NULL)) {
        goto _quit;
    }

    s_tcp_conn_pool = conn_pool;

    conn_hash = dpdk_hash_create(DP_TCP_CONN_MAX_PER_THREAD, sizeof(struct tcp6_tuple), tlv_hw_numa_id, memcmp);
    if (UNLIKELY(conn_hash == NULL)) {
        goto _quit;
    }

    s_tcp_conn_hash = conn_hash;

    ret = tcp4_thread_create();
    if (UNLIKELY(ret != 0)) {
        goto _quit;
    }

    return 0;

_quit:
    tcp_thread_destroy();
    return -1;
}

void tcp_thread_destroy(void)
{
    if (s_tcp_conn_pool != NULL) {
        dpdk_ring_destroy(s_tcp_conn_pool);
        s_tcp_conn_pool = NULL;
    }

    if (s_tcp_conn_hash != NULL) {
        dpdk_hash_destroy(s_tcp_conn_hash);
        s_tcp_conn_hash = NULL;
    }

    tcp4_thread_destroy();
}