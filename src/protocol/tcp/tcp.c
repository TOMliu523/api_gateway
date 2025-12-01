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
#include "vserver.h"
#include "tcp_inner.h"
#include "dpdk_hash.h"
#include "dpdk_core.h"
#include "dpdk_limits.h"
#include "dpdk_atomic.h"

#define TCP_CONN_CACHE_MAX 256
#define TCP_CONN_EXPIRE_SECOND 900

#define TCP_FIXED_NO_OPTION_MIN (sizeof(struct dpdk_eth_hdr) + sizeof(struct dpdk_tcp_hdr))
#define TCP_IP4_NO_OPTION_MIN (TCP_FIXED_NO_OPTION_MIN + sizeof(struct dpdk_ip4_hdr))
#define TCP_IP6_NO_OPTION_MIN (TCP_FIXED_NO_OPTION_MIN + sizeof(struct dpdk_ip6_hdr))

static __thread void *sp_tcp_conn_pool;
static __thread void *sp_tcp_conn_hash;
static __thread int s_cache_count;
static __thread void *sp_tcp_conn_cache[TCP_CONN_CACHE_MAX];
static struct tcp_ops *sp_tcp4_ops;
static UNUSED struct tcp_ops *sp_tcp6_ops;

static INLINE void *_tcp_conn_pop(void)
{
    int ret = 0;

    if (UNLIKELY(s_cache_count == 0)) {
        ret = dpdk_mempool_pop(sp_tcp_conn_pool, sp_tcp_conn_cache, ARR_NUMS(sp_tcp_conn_cache));
        if (LIKELY(ret == 0)) {
            s_cache_count = ARR_NUMS(sp_tcp_conn_cache);
        } else {
            s_cache_count = dpdk_mempool_avail_count(sp_tcp_conn_pool);
            if (LIKELY(s_cache_count != 0)) {
                dpdk_mempool_pop(sp_tcp_conn_pool, sp_tcp_conn_cache, s_cache_count);
            } else {
                LOG_WARN("Thread(%d) Conn table is empty.", tlv_thread_id);
                return NULL;
            }
        }
    }

    return sp_tcp_conn_cache[--s_cache_count];
}

static INLINE void __tcp_conn_push_bulk(void *cache[], int *out_count, void *conn[], int in_count)
{
    int i = 0;
    int count = *out_count;

    switch (in_count) {
    case 7:
        cache[count++] = conn[i++];
        FALLTHROUGH;
    case 6:
        cache[count++] = conn[i++];
        FALLTHROUGH;
    case 5:
        cache[count++] = conn[i++];
        FALLTHROUGH;
    case 4:
        cache[count++] = conn[i++];
        FALLTHROUGH;
    case 3:
        cache[count++] = conn[i++];
        FALLTHROUGH;
    case 2:
        cache[count++] = conn[i++];
        FALLTHROUGH;
    case 1:
        cache[count++] = conn[i++];
        break;
    default:
        dpdk_memcpy(cache + count, conn, in_count * sizeof(*conn));
        count += in_count;
        break;
    }

    *out_count = count;
}

static INLINE void _tcp_conn_push_bulk(void *conn[], int count)
{
    if (s_cache_count + count <= TCP_CONN_CACHE_MAX) {
        __tcp_conn_push_bulk(sp_tcp_conn_cache, &s_cache_count, conn, count);
    } else {
        int half = TCP_CONN_CACHE_MAX / 2;
        int diff = s_cache_count - half;

        if (diff < 0) {
            diff = -diff;
            __tcp_conn_push_bulk(sp_tcp_conn_cache, &s_cache_count, conn, diff);
            dpdk_mempool_push(sp_tcp_conn_pool, conn + diff, count - diff);
        } else {
            dpdk_mempool_push(sp_tcp_conn_pool, sp_tcp_conn_cache + half, diff);
            s_cache_count = half;
            dpdk_mempool_push(sp_tcp_conn_pool, conn, count);
        }
    }
}

static INLINE void _tcp_conn_push(void *conn)
{
    _tcp_conn_push_bulk(&conn, 1);
}

static INLINE void _tcp_win_ack(struct tcb *tcb, uint32_t ack)
{
    tcb->snd.una += 1;
}

static INLINE void tcp_direct_close(struct tcp_conn *conn)
{

}

static INLINE void tcp_conn_close(struct tcp_conn *conn)
{

}

static INLINE void tcp_conn_state_set(struct tcp_conn *conn, enum TCP_STATE state)
{
    conn->state = state;
}

static INLINE int _tcp_listen_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    int ret = 0;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    switch (tcphdr->tcp_flags & TCP_F_MASK) {
    case TCP_F_SYN:
        ret = ops->syn_ack_reply(mbuf, conn);
        if (UNLIKELY(ret != 0)) {
            tcp_conn_close(conn);
            return -1;
        }

        pktmbuf_send(mbuf);
        tcp_conn_state_set(conn, TCP_SYN_RECEIVE);
        return 0;
    case TCP_F_RST:
    case TCP_F_FIN_RST:
    case TCP_F_SYN_RST:
    case TCP_F_RST_ACK:
    case TCP_F_FIN_SYN_RST:
    case TCP_F_FIN_RST_ACK:
    case TCP_F_SYN_RST_ACK:
    case TCP_F_ALL:
        return -1;
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
        return 0;
    }
    default: return -1; // TCP_F_FIN TCP_F_SYN_FIN TCP_F_NONE
    }
}

static INLINE int _tcp_close_process(struct dpdk_mbuf *mbuf, const struct tcp_ops *ops)
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
    case TCP_F_ALL: return -1;
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
        return 0;
    }
}

static INLINE int _tcp_server_trigger(struct dpdk_mbuf *mbuf, struct tcp_conn *conn)
{
    struct tcp_context ctx = {0};



    return 0;
}

static INLINE int _tcp_syn_receive_ack_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    int ret = 0;
    int len = 0;
    uint32_t seq = 0;
    uint32_t ack = 0;
    uint8_t *data = NULL;
    struct tcb *tcb = NULL;
    struct tcp_opt_info info = {0};
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    data = (uint8_t *)(tcphdr + 1);
    len = tcp_header_len(tcphdr) - sizeof(struct dpdk_tcp_hdr);

    ret = tcp_option_get(&info, data, len);
    if (UNLIKELY(ret != 0)) {
        return -1;
    }

    // timestamp check
    tcb = &conn->tcb;
    if (UNLIKELY(tcb->send_ts_ok && info.tsval < tcb->ts_recent)) {
        return -1;
    }

    seq = dpdk_be_to_cpu_32(tcphdr->sent_seq);
    ack = dpdk_be_to_cpu_32(tcphdr->recv_ack);
    if (UNLIKELY(seq != tcb->rcv.nxt || ack != tcb->snd.nxt)) {
        return -1;
    }

    if (tcb->send_ts_ok) {
        tcb->ts_recent = info.tsval;
    }

    conn->tcb.snd.una += 1;
    tcp_conn_state_set(conn, TCP_ESTABLISHED);

    switch (conn->type) {
    case PROTO_TCP:
        ret = _tcp_server_trigger(mbuf, conn);
        if (UNLIKELY(ret != 0)) {
            ops->rst_ts_reply(mbuf, conn);
            pktmbuf_send(mbuf);
            tcp_direct_close(conn);
        }
        break;
    case PROTO_UDP:
        break;
    case PROTO_HTTP:
        break;
    case PROTO_HTTPS:
        break;
    case PROTO_HTTP2:
        break;
    default:
        break;
    }

    return 0;
}

static INLINE int _tcp_syn_receive_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, struct tcp_ops *ops)
{
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    switch (tcphdr->tcp_flags & TCP_F_MASK) {
    case TCP_F_ACK: return _tcp_syn_receive_ack_process(mbuf, conn, ops);
    case TCP_F_FIN:
        break;
    default:
        break;
    }

    return -1;
}

static INLINE int _tcp_syn_sent_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

static INLINE int _tcp_fin_wait1_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

static INLINE int _tcp_fin_wait2_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

static INLINE int _tcp_close_wait_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

static INLINE int _tcp_last_ack_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

static INLINE int _tcp_closing_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

static INLINE int _tcp_time_wait_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const struct tcp_ops *ops)
{
    return 0;
}

int tcp_state_process(struct dpdk_mbuf *mbuf, struct tcp_conn *conn, const void *arg)
{
    int state = 0;
    struct tcp_ops *ops = (struct tcp_ops *)arg;

    state = (conn != NULL) ? conn->state : TCP_CLOSE;
    switch (state) {
    case TCP_ESTABLISHED: break;
    case TCP_LISTEN: return _tcp_listen_process(mbuf, conn, ops);
    case TCP_CLOSE: return _tcp_close_process(mbuf, ops);
    case TCP_SYN_SENT: return _tcp_syn_sent_process(mbuf, conn, ops);
    case TCP_SYN_RECEIVE: return _tcp_syn_receive_process(mbuf, conn, ops);
    case TCP_FIN_WAIT1: return _tcp_fin_wait1_process(mbuf, conn, ops);
    case TCP_FIN_WAIT2: return _tcp_fin_wait2_process(mbuf, conn, ops);
    case TCP_CLOSE_WAIT: return _tcp_close_wait_process(mbuf, conn, ops);
    case TCP_LAST_ACK: return _tcp_last_ack_process(mbuf, conn, ops);
    case TCP_CLOSING: return _tcp_closing_process(mbuf, conn, ops);
    case TCP_TIME_WAIT: return _tcp_time_wait_process(mbuf, conn, ops);
    default: LOG_ERROR("Looking at this code… how did it end up like this?"); return -1;
    }

    return -1;
}

void *tcp_conn_client_create(int af, uint16_t port, uint32_t vs_id, struct tcp_tuple *tuple, int type)
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
    conn->type = type;
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
    void **datas = (void **)blk->conns;
    const void **keys = (const void **)blk->keys;

    dpdk_hash_lookup_bulk(sp_tcp_conn_hash, keys, count, hit_mask, datas);
}

int tcp_thread_resource_init(void)
{
    int ret = 0;
    void *conn_hash = NULL;
    void *conn_pool = NULL;
    char name[CACHE_LINE] = "";

    snprintf(name, sizeof(name), "TCP_CONN_%u_%u", tlv_thread_id, (uint32_t)tlv_dp->off_time);
    conn_pool = dpdk_pool_ss_create(name, DP_TCP_CONN_MAX_PER_THREAD, sizeof(struct tcp_conn), tlv_hw_numa_id);
    if (UNLIKELY(conn_pool == NULL)) {
        goto _quit;
    }

    sp_tcp_conn_pool = conn_pool;

    conn_hash = dpdk_hash_create(DP_TCP_CONN_MAX_PER_THREAD, sizeof(struct tcp4_tuple), tlv_hw_numa_id, memcmp);
    if (UNLIKELY(conn_hash == NULL)) {
        goto _quit;
    }

    sp_tcp_conn_hash = conn_hash;

    ret = tcp4_thread_resource_init();
    if (UNLIKELY(ret != 0)) {
        goto _quit;
    }

    sp_tcp4_ops = tcp4_thread_ops_get();
    return 0;

_quit:
    tcp_thread_resource_fini();
    return -1;
}

void tcp_thread_resource_fini(void)
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