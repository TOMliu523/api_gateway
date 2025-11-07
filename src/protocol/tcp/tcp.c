/*****************************************************************************
 * filename: tcp.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "ip4.h"
#include "ip6.h"
#include "tcp.h"
#include "type.h"
#include "tcp_inner.h"
#include "dpdk_hash.h"
#include "dpdk_core.h"
#include "dpdk_limits.h"
#include "dpdk_atomic.h"

#define TCP_CONN_CACHE_MAX 64
#define TCP_CONN_EXPIRE_SECOND 900

#define TCP_FIXED_NO_OPTION_MIN (sizeof(struct dpdk_eth_hdr) + sizeof(struct dpdk_tcp_hdr))
#define TCP_IP4_NO_OPTION_MIN (TCP_FIXED_NO_OPTION_MIN + sizeof(struct dpdk_ip4_hdr))
#define TCP_IP6_NO_OPTION_MIN (TCP_FIXED_NO_OPTION_MIN + sizeof(struct dpdk_ip6_hdr))

static __thread void *sp_tcp_conn_pool;
static __thread void *sp_tcp_conn_hash;
static __thread int s_cache_count;
static __thread void *sp_tcp_conn_cache[TCP_CONN_CACHE_MAX];

static INLINE void *_tcp_conn_pop(void)
{
    if (UNLIKELY(s_cache_count == 0)) {
        s_cache_count = dpdk_ring_sc_pop(sp_tcp_conn_pool, sp_tcp_conn_cache, ARR_NUMS(sp_tcp_conn_cache));
        if (UNLIKELY(s_cache_count == 0)) {
            LOG_WARN("Thread(%d) Conn table is empty.", tlv_thread_id);
            return NULL;
        }
    }

    return sp_tcp_conn_cache[--s_cache_count];
}

static INLINE void _tcp_conn_push_bulk(void *conn[], int count)
{
    if (s_cache_count + count <= TCP_CONN_CACHE_MAX) {
        dpdk_memcpy(sp_tcp_conn_cache + s_cache_count, conn, count * sizeof(*conn));
        s_cache_count += count;
    } else {
        int n_fill = TCP_CONN_CACHE_MAX - s_cache_count;
        if (UNLIKELY(n_fill != 0)) {
            dpdk_memcpy(sp_tcp_conn_cache + s_cache_count, conn, n_fill);
            s_cache_count = TCP_CONN_CACHE_MAX;
        }
        dpdk_ring_sp_push(sp_tcp_conn_pool, conn + n_fill, count - n_fill);
    }
}

static INLINE void _tcp_conn_push(void *conn)
{
    _tcp_conn_push_bulk(&conn, 1);
}

static INLINE void tcp_conn_close(struct tcp_conn *conn)
{

}

static void _tcp_listen_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, struct tcp_ops *ops)
{
    int ret = 0;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    switch (tcphdr->tcp_flags & TCP_F_MASK) {
    case TCP_F_SYN:
        ret = ops->syn_ack_reply(mbuf, conn);
        if (UNLIKELY(ret != 0)) {
            pktmbuf_drop(mbuf);
            tcp_conn_close(conn);
            break;
        }

        pktmbuf_send(mbuf);
        break;
    case TCP_F_RST:
    case TCP_F_FIN_RST:
    case TCP_F_SYN_RST:
    case TCP_F_RST_ACK:
    case TCP_F_FIN_SYN_RST:
    case TCP_F_FIN_RST_ACK:
    case TCP_F_SYN_RST_ACK:
    case TCP_F_ALL:
        pktmbuf_drop(mbuf);
        break;
    case TCP_F_ACK:
    case TCP_F_FIN_ACK:
    case TCP_F_SYN_ACK:
    case TCP_F_FIN_SYN_ACK:
    {
        uint32_t seq = 0;
        uint32_t ack = 0;

        seq = dpdk_be_to_cpu_32(tcphdr->recv_ack);
        ack = dpdk_be_to_cpu_32(tcphdr->sent_seq) + DPDK_HEADROOM(mbuf)->payload_len;
        ops->rst_reply(mbuf, seq, ack);
        break;
    }
    default: // TCP_F_FIN TCP_F_SYN_FIN TCP_F_NONE
        pktmbuf_drop(mbuf);
        break;
    }
}

static void _tcp_close_process(struct dpdk_mbuf *mbuf, struct tcp_ops *ops)
{
    uint32_t ack = 0;
    uint32_t seq = 0;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    switch (tcphdr->tcp_flags & TCP_F_MASK) {
    case TCP_F_RST:
    case TCP_F_FIN_RST:
    case TCP_F_SYN_RST:
    case TCP_F_RST_ACK:
    case TCP_F_FIN_SYN_RST:
    case TCP_F_FIN_RST_ACK:
    case TCP_F_SYN_RST_ACK:
    case TCP_F_ALL:
        pktmbuf_drop(mbuf);
        break;
    case TCP_F_ACK:
    case TCP_F_FIN_ACK:
    case TCP_F_SYN_ACK:
    case TCP_F_FIN_SYN_ACK:
        seq = dpdk_be_to_cpu_32(tcphdr->recv_ack);
        FALLTHROUGH;
    default: // TCP_F_FIN TCP_F_SYN TCP_F_FIN_SYN TCP_F_NONE
        ack = dpdk_be_to_cpu_16(tcphdr->sent_seq) + DPDK_HEADROOM(mbuf)->payload_len;
        ops->rst_reply(mbuf, seq, ack);
        pktmbuf_send(mbuf);
        break;
    }
}

void tcp_state_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, void *arg)
{
    int state = 0;
    struct tcp_ops *ops = (struct tcp_ops *)arg;

    state = (conn != NULL) ? conn->state : TCP_CLOSE;
    switch (state) {
    case TCP_ESTABLISHED:
        break;
    case TCP_LISTEN:
        _tcp_listen_process(mbuf, conn, ops);
        break;
    case TCP_CLOSE:
        _tcp_close_process(mbuf, ops);
        break;
    case TCP_SYN_SENT:
        break;
    case TCP_SYN_RECEIVE:
        break;
    case TCP_FIN_WAIT1:
        break;
    case TCP_FIN_WAIT2:
        break;
    case TCP_CLOSE_WAIT:
        break;
    case TCP_LAST_ACK:
        break;
    case TCP_CLOSING:
        break;
    case TCP_TIME_WAIT:
        break;
    default:
        LOG_ERROR("Looking at this code… how did it end up like this?");
        pktmbuf_drop(mbuf);
        return;
    }
}

void *tcp_conn_client_create(int af, uint16_t port, uint32_t vs_id, struct tcp_tuple *tuple)
{
    int ret = 0;
    struct tcp_conn *conn = NULL;

    conn = _tcp_conn_pop();
    if (UNLIKELY(conn == NULL)) {
        return NULL;
    }

    conn->side = TCP_CLIENTSIDE;
    conn->af = af;
    conn->state = TCP_LISTEN;
    conn->port = port;
    conn->vs_id = vs_id;
    conn->expire_time = tlv_dp->off_time + TCP_CONN_EXPIRE_SECOND;
    dpdk_atomic16_init(&conn->refcnt);

    ret = dpdk_hash_add_kv(sp_tcp_conn_hash, tuple, conn);
    if (UNLIKELY(ret < 0)) {
        _tcp_conn_push(conn);
        return NULL;
    }

    return conn;
}

void tcp_conn_lookup(struct tcp4_lookup_blk *blk)
{
    int count = blk->count;
    uint64_t *hit_mask = &blk->resutl;
    void **data = (void **)blk->conns;
    const void **keys = (const void **)blk->keys;

    dpdk_hash_lookup_bulk(sp_tcp_conn_hash, keys, count, hit_mask, data);
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

    sp_tcp_conn_pool = conn_pool;

    conn_hash = dpdk_hash_create(DP_TCP_CONN_MAX_PER_THREAD, sizeof(struct tcp6_tuple), tlv_hw_numa_id, memcmp);
    if (UNLIKELY(conn_hash == NULL)) {
        goto _quit;
    }

    sp_tcp_conn_hash = conn_hash;

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
    if (sp_tcp_conn_pool != NULL) {
        dpdk_ring_destroy(sp_tcp_conn_pool);
        sp_tcp_conn_pool = NULL;
    }

    if (sp_tcp_conn_hash != NULL) {
        dpdk_hash_destroy(sp_tcp_conn_hash);
        sp_tcp_conn_hash = NULL;
    }

    tcp4_thread_destroy();
}