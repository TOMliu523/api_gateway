/*****************************************************************************
 * filename: tcp4.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "ip4.h"
#include "log.h"
#include "tcp.h"
#include "type.h"
#include "vserver.h"
#include "dpdk_crc.h"
#include "tcp_inner.h"
#include "dpdk_type.h"
#include "dpdk_core.h"
#include "dpdk_bitops.h"

#define TCP_IP4_IRS_SECRETKEY 0x49893759
#define TCP_IP4_HDR_MIN (sizeof(struct dpdk_eth_hdr) + sizeof(struct dpdk_ip4_hdr))
#define TCP_IP_HDR_PAYLOAD_MIN (sizeof(struct dpdk_ip4_hdr) + sizeof(struct dpdk_tcp_hdr))
#define TCP_HDR_MIN (TCP_IP4_HDR_MIN + sizeof(struct dpdk_tcp_hdr))

#define TCP_IP4_MSS (tlv_dp->mtu - TCP_IP_HDR_PAYLOAD_MIN)
#define TCP_IP4_MSS_DEFAULT (576 - TCP_IP_HDR_PAYLOAD_MIN)

static void _tcp4_rst_reply(struct dpdk_mbuf *m, uint32_t seq, uint32_t ack);
static int _tcp4_syn_ack_reply(struct dpdk_mbuf *m, struct tcp_conn *conn);

static __thread uint64_t s_inv_hz_us;
static __thread struct vserver4_kv_blk *sp_vs4_blk;
static __thread struct tcp4_lookup_blk *sp_tcp4_lookup_blk;
static __thread struct tcp_ops s_tcp_ops = {
    .rst_reply = _tcp4_rst_reply,
    .syn_ack_reply = _tcp4_syn_ack_reply,
};

static int _tcp4_validate_and_prepare(struct tcp4_lookup_blk *blk, void *data[], int count)
{
    int n = 0;
    int payload_len = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct dpdk_tcp_hdr *tcphdr = NULL;

    blk->mbufs = data;
    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ip4hdr = DPDK_HEADROOM(mbuf)->l3;
        tcphdr = DPDK_HEADROOM(mbuf)->l4;

        payload_len = dpdk_be_to_cpu_32(ip4hdr->total_length) - mbuf->l3_len - tcp_header_len(tcphdr);
        if (UNLIKELY(payload_len < 0)) {
            pktmbuf_drop(mbuf);
            continue;
        }

        if (UNLIKELY(!tcp4_mbuf_cksum_verify(mbuf, tcphdr))) {
            pktmbuf_drop(mbuf);
            continue;
        }

        DPDK_HEADROOM(mbuf)->payload_len = payload_len;

        blk->keys[n]->dip = ip4hdr->dst_addr;
        blk->keys[n]->sip = ip4hdr->src_addr;
        blk->keys[n]->sport = tcphdr->src_port;
        blk->keys[n]->dport = tcphdr->dst_port;
        blk->keys[n]->version = 0;
        blk->mbufs[n] = mbuf;

        n += 1;
    }

    return n;
}

static INLINE void _tcp4_conn_lookup(struct tcp4_lookup_blk *blk, int n)
{
    blk->resutl = 0;
    blk->count = n;
    tcp_conn_lookup(blk);
}

static int _tcp4_header_syn_process(const struct dpdk_mbuf *mbuf)
{
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    if (UNLIKELY((tcphdr->tcp_flags & TCP_F_MASK) != TCP_F_SYN)) {
        return -1;
    }

    // SYN packets are not allowed to carry any data
    ip4hdr = DPDK_HEADROOM(mbuf)->l3;
    if (UNLIKELY(mbuf->l3_len + tcp_header_len(tcphdr) != dpdk_cpu_to_be_16(ip4hdr->total_length))) {
        return -1;
    }

    // TODO Check the timestamp, and discard packets whose expiration time is too long.

    return 0;
}

static INLINE int _tcp4_dispatch(struct vserver4_kv_blk *vs4_blk, struct tcp4_lookup_blk *blk)
{
    int n = 0;
    int ret = 0;
    int count = blk->count;
    uint64_t result = blk->resutl;
    struct dpdk_mbuf *mbuf = NULL;
    struct tcp4_tuple *tuple = NULL;

    vs4_blk->mbufs = blk->mbufs;
    for (int i = 0; i < count; i++) {
        if (dpdk_bit_test_u64(result, i)) {
            // TODO has tcp connection, entry tcp stack
        } else {
            mbuf = blk->mbufs[i];

            ret = _tcp4_header_syn_process(mbuf);
            if (UNLIKELY(ret != 0)) {
                tcp_state_process(mbuf, NULL, &s_tcp_ops);
                continue;
            }

            tuple = &blk->tuples[i];
            vs4_blk->keys[n]->addr = tuple->dip;
            vs4_blk->keys[n]->port = tuple->dport;
            vs4_blk->keys[n]->protocol = PROTO_TCP; // TCP
            vs4_blk->mbufs[n] = mbuf;

            n += 1;
        }
    }

    vs4_blk->count = n;
    return n;
}

static INLINE void _tcp4_vs_lookup(struct vserver4_kv_blk *vs4_blk, int n)
{
    vs4_blk->count = n;
    vs4_blk->result = 0;
    vserver4_lookup(vs4_blk);
}

static INLINE int _tcp4_conn_create(struct vserver_v4 *v4, struct dpdk_mbuf *mbuf)
{
    struct tcp_conn *conn = NULL;
    struct tcp4_tuple tuple = {0};
    struct dpdk_ip4_hdr *ip4hdr = DPDK_HEADROOM(mbuf)->l3;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    tuple.sip = ip4hdr->dst_addr;
    tuple.dip = ip4hdr->src_addr;
    tuple.sport = tcphdr->src_port;
    tuple.dport = tcphdr->dst_port;
    tuple.version = 0;

    conn = tcp_conn_client_create(AF_INET, mbuf->port, v4->vs.id, (struct tcp_tuple *)&tuple);
    if (UNLIKELY(conn == NULL)) {
        return -1;
    }

    tcp_state_process(mbuf, conn, &s_tcp_ops);
    return 0;
}

static INLINE void _tcp4_rst_reply(struct dpdk_mbuf *m, uint32_t seq, uint32_t ack)
{
    uint16_t total_len = 0;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(m)->l4;

    tcp_header_init(tcphdr, dpdk_be_to_cpu_16(tcphdr->dst_port), dpdk_be_to_cpu_16(tcphdr->src_port), seq, ack,
        ((ack != 0) ? TCP_F_RST_ACK : TCP_F_RST), 0, sizeof(struct dpdk_tcp_hdr));
    total_len = sizeof(struct dpdk_ip4_hdr) + sizeof(struct dpdk_tcp_hdr);
    ip4_pktmbuf_replay(m, IPPROTO_TCP, total_len);
    l2_pktmbuf_repay(m);
    dpdk_pktmbuf_trim(m, TCP_HDR_MIN);
}

static INLINE uint32_t _tcp4_irs_get(const struct dpdk_tcp_hdr *tcphdr, const struct dpdk_ip4_hdr *ip4hdr)
{
    uint32_t irs = 0;

    struct {
        uint32_t sip;
        uint32_t dip;
        uint16_t sport;
        uint16_t dport;
        uint32_t secretkey;
    } f = {
        .sip = ip4hdr->src_addr,
        .dip = ip4hdr->dst_addr,
        .sport = tcphdr->src_port,
        .dport = tcphdr->dst_port,
        .secretkey = TCP_IP4_IRS_SECRETKEY,
    };

    irs = dpdk_hash_crc(&f, sizeof(f));
    return (irs + ((uint32_t)(s_inv_hz_us * dpdk_timer_cycles()) >> 2));
}

static INLINE void _tcp_tcb_init(struct tcb *tcb, struct dpdk_tcp_hdr *tcphdr, struct dpdk_ip4_hdr *ip4hdr)
{
    uint32_t iss = _tcp4_irs_get(tcphdr, ip4hdr);

    tcb->snd.una = tcb->snd.nxt = tcb->snd.iss = iss;
    tcb->snd.wnd = TCP_HDR_WIN;
    tcb->send_win_shift = TCP_WIN_SCALE;
}

static INLINE int _tcp4_pktmbuf_syn_parse(struct dpdk_tcp_hdr *tcphdr, struct tcp_conn *conn)
{
    int ret = 0;
    int option_len = 0;
    uint8_t *option = NULL;
    struct tcb *tcb = NULL;
    struct tcp_opt_info opt_info = {0};

    // option
    option = (uint8_t *)(tcphdr + 1);
    option_len = tcp_header_len(tcphdr) - sizeof(struct dpdk_tcp_hdr);
    ret = tcp_option_get(&opt_info, option, option_len);
    if (UNLIKELY(ret != 0)) {
        return -1;
    }

    tcb = &conn->tcb;
    tcb->recv_win_shift = opt_info.scale;
    tcb->send_ts_ok = opt_info.send_ts_ok;
    tcb->mss = MIN(((opt_info.mss != 0) ? opt_info.mss : TCP_IP4_MSS_DEFAULT), TCP_IP4_MSS);

    if (opt_info.tsval != 0) {
        tcb->send_ts_ok = TCP_OPTION_TS_OK;
    }

    tcb->rcv.irs = dpdk_be_to_cpu_32(tcphdr->sent_seq);
    tcb->rcv.nxt = tcb->rcv.irs;
    tcb->rcv.wnd = dpdk_be_to_cpu_32(tcphdr->rx_win);
    tcb->rcv.end = tcb->rcv.nxt + tcb->rcv.wnd;

    return 0;
}

static INLINE int _tcp4_syn_ack_reply(struct dpdk_mbuf *m, struct tcp_conn *conn)
{
    int ret = 0;
    struct dpdk_ip4_hdr *ip4hdr = DPDK_HEADROOM(m)->l3;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(m)->l4;

    ret = _tcp4_pktmbuf_syn_parse(tcphdr, conn);
    if (UNLIKELY(ret != 0)) {
        return -1;
    }

    _tcp_tcb_init(&conn->tcb, tcphdr, ip4hdr);

    return 0;
}

static void _tcp4_vs_process(struct vserver4_kv_blk *vs4_blk)
{
    int ret = 0;
    int count = vs4_blk->count;
    struct vserver_v4 *v4 = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    uint64_t result = vs4_blk->result;

    for (int i = 0; i < count; i++) {
        if (dpdk_bit_test_u64(result, i)) {
            v4 = vs4_blk->data[i];
            mbuf = vs4_blk->mbufs[i];

            ret = _tcp4_conn_create(v4, mbuf);
            if (LIKELY(ret == 0)) {
                continue;
            }
        }

        // TODO entry tck stack
        tlv_drop->data[tlv_drop->count++] = vs4_blk->mbufs[i];
        vs4_blk->mbufs[i] = NULL;
    }
}

static INLINE void _tcp4_conn_process(struct tcp4_lookup_blk *blk)
{
    int n = 0;
    struct vserver4_kv_blk *vs4_blk = sp_vs4_blk;

    n = _tcp4_dispatch(vs4_blk, blk);
    if (n == 0) {
        return;
    }

    _tcp4_vs_lookup(vs4_blk, n);
    _tcp4_vs_process(vs4_blk);
}

void tcp4_process(void *data[], int count)
{
    int n = 0;
    struct tcp4_lookup_blk *blk = sp_tcp4_lookup_blk;

    n = _tcp4_validate_and_prepare(blk, data, count);
    if (UNLIKELY(n == 0)) {
        return;
    }

    _tcp4_conn_lookup(blk, n);
    _tcp4_conn_process(blk);
}

int tcp4_thread_create(void)
{
    struct tcp4_lookup_blk *blk = NULL;
    struct vserver4_kv_blk *v4_kv_blk = NULL;

    blk = dpdk_malloc_numa(sizeof(*blk), tlv_hw_numa_id);
    if (UNLIKELY(blk == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(blk, 0, sizeof(*blk));

    for (int i = 0; i < ARR_NUMS(blk->keys); i++) {
        blk->keys[i] = &blk->tuples[i];
    }

    blk->mbufs = tlv_tcp4->data;
    sp_tcp4_lookup_blk = blk;

    v4_kv_blk = dpdk_malloc_numa(sizeof(*v4_kv_blk), tlv_hw_numa_id);
    if (UNLIKELY(v4_kv_blk == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(v4_kv_blk, 0, sizeof(*v4_kv_blk));

    for (int i = 0; i < ARR_NUMS(v4_kv_blk->keys); i++) {
        v4_kv_blk->keys[i] = &v4_kv_blk->v4_keys[i];
    }

    sp_vs4_blk = v4_kv_blk;

    s_inv_hz_us = 1000000.0 / (double) dpdk_timer_hz();
    return 0;

_quit:
    tcp4_thread_destroy();
    return -1;
}

void tcp4_thread_destroy(void)
{
    if (sp_tcp4_lookup_blk != NULL) {
        dpdk_free(sp_tcp4_lookup_blk);
        sp_tcp4_lookup_blk = NULL;
    }

    if (sp_vs4_blk != NULL) {
        dpdk_free(sp_vs4_blk);
        sp_vs4_blk = NULL;
    }
}