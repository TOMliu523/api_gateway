/*****************************************************************************
 * filename: tcp4.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "ip4.h"
#include "log.h"
#include "tcp.h"
#include "util.h"
#include "type.h"
#include "vserver.h"
#include "dpdk_crc.h"
#include "tcp_inner.h"
#include "dpdk_type.h"
#include "dpdk_core.h"
#include "dpdk_bitops.h"

#define TCP_IP4_IRS_SECRETKEY 0x49893759
#define TCP_IP4_HDR_MIN (L2_MAC_HDR_MIN + IP4_HDR_MIN)
#define TCP_IP_HDR_PAYLOAD_MIN (IP4_HDR_MIN + TCP_HDR_MIN)
#define TCP_PKTMBUF_MIN (TCP_IP4_HDR_MIN + TCP_HDR_MIN)

#define TCP_IP4_MSS (tlv_dp->mtu - TCP_IP_HDR_PAYLOAD_MIN)
#define TCP_IP4_MSS_DEFAULT (576 - TCP_IP_HDR_PAYLOAD_MIN)

static int _tcp4_syn_ack_reply(struct dpdk_mbuf *m, struct tcp_conn *conn);
static void _tcp4_rst_reply(struct dpdk_mbuf *m, uint32_t seq, uint32_t ack);
static INLINE void _tcp4_rst_ts_reply(struct dpdk_mbuf *m, struct tcp_conn *conn);

static __thread uint64_t s_tcp_ts_mul;
static __thread struct vserver4_kv_blk *sp_vs4_blk;
static __thread struct tcp4_lookup_blk *sp_tcp4_lookup_blk;
static struct tcp_ops s_tcp_ops = {
    .syn_ack_reply = _tcp4_syn_ack_reply,
    .rst_reply = _tcp4_rst_reply,
    .rst_ts_reply = _tcp4_rst_ts_reply,
};

static INLINE int _tcp4_state_process(struct dpdk_mbuf *m, struct tcp_conn *conn)
{
    return tcp_state_process(m, conn, &s_tcp_ops);
}

static INLINE void _tcp4_pktmbuf_verify(struct dpdk_mbuf *m, struct dpdk_tcp_hdr *tcphdr, void *l3hdr)
{
    if (LIKELY(tlv_tx_offload[m->port] & DPDK_TCP_TX_CKSUM) != 0) {
        tcphdr->cksum = dpdk_ip4_phdr_cksum(l3hdr, m->ol_flags);
        m->ol_flags |= DPDK_TCP_TX_CKSUM;
    } else {
        dpdk_tcp4_mbuf_cksum(m, l3hdr);
    }
}

static int _tcp4_validate_and_prepare(struct tcp4_lookup_blk *blk, void *data[], int count)
{
    int n = 0;
    int payload_len = 0;
    struct dpdk_mbuf *m = NULL;
    struct tcp4_tuple *tuple = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct dpdk_tcp_hdr *tcphdr = NULL;

    blk->mbufs = data;
    for (int i = 0; i < count; i++) {
        m = data[i];
        ip4hdr = DPDK_HEADROOM(m)->l3;
        tcphdr = DPDK_HEADROOM(m)->l4;

        payload_len = dpdk_be_to_cpu_16(ip4hdr->total_length) - dpdk_ip4_header_len(ip4hdr) - tcp_header_len(tcphdr);
        if (UNLIKELY(payload_len < 0)) {
            pktmbuf_drop(m);
            continue;
        }

        if (UNLIKELY(!tcp4_mbuf_cksum_verify(m, tcphdr))) {
            pktmbuf_drop(m);
            continue;
        }

        DPDK_HEADROOM(m)->payload_len = payload_len;

        tuple = blk->keys[n];
        tuple->sip = ip4hdr->src_addr;
        tuple->dip = ip4hdr->dst_addr;
        tuple->sport = tcphdr->src_port;
        tuple->dport = tcphdr->dst_port;
        tuple->version = 0;
        blk->mbufs[n] = m;

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

static int _tcp4_header_syn_process(struct dpdk_mbuf *m)
{
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(m)->l4;

    if (UNLIKELY((tcphdr->tcp_flags & TCP_F_MASK) != TCP_F_SYN)) {
        _tcp4_state_process(m, NULL);
        return -1;
    }

    /*
     * RFC 9293 does not prohibit SYN segments from carrying data;
     * however, this implementation does not allow any data in SYN packets.
     */
    ip4hdr = DPDK_HEADROOM(m)->l3;
    if (UNLIKELY(dpdk_ip4_header_len(ip4hdr) + tcp_header_len(tcphdr) != dpdk_cpu_to_be_16(ip4hdr->total_length))) {
        pktmbuf_drop(m);
        return -1;
    }

    // TODO Check the timestamp, and discard packets whose expiration time is too long.

    return 0;
}

static INLINE int _tcp4_conn_dispatch(struct vserver4_kv_blk *vs4_blk, struct tcp4_lookup_blk *blk)
{
    int n = 0;
    int ret = 0;
    int count = blk->count;
    uint64_t result = blk->resutl;
    struct dpdk_mbuf *m = NULL;
    struct tcp4_tuple *tuple = NULL;
    struct vserver4_key *key = NULL;

    vs4_blk->mbufs = blk->mbufs;
    for (int i = 0; i < count; i++) {
        if (dpdk_bit_test_u64(result, i)) {
            m = blk->mbufs[i];
            ret = _tcp4_state_process(m, blk->conns[i]);
            if (UNLIKELY(ret != 0)) {
                pktmbuf_drop(m);
                continue;
            }
        } else {
            m = blk->mbufs[i];

            ret = _tcp4_header_syn_process(m);
            if (UNLIKELY(ret != 0)) {
                continue;
            }

            tuple = blk->keys[i];

            key = vs4_blk->keys[n];
            key->addr = tuple->dip;
            key->port = tuple->dport;
            key->protocol = PROTO_TCP; // TCP

            vs4_blk->mbufs[n] = m;
            vs4_blk->tuple[n] = blk->keys[i];

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
    vserver4_lookup_bulk(vs4_blk);
}

static INLINE int _tcp4_conn_create(struct vserver4 *v4, struct dpdk_mbuf *m, struct tcp4_tuple *tuple, struct vserver4 *vs4)
{
    struct tcp_conn *conn = NULL;

    conn = tcp_conn_client_create(AF_INET, m->port, v4->vs.id, (struct tcp_tuple *)tuple, vs4->type);
    if (UNLIKELY(conn == NULL)) {
        return -1;
    }

    return _tcp4_state_process(m, conn);
}

static INLINE void _tcp4_pktmbuf_rst_ack(struct dpdk_mbuf *m, struct dpdk_tcp_hdr *tcphdr, struct tcp_conn *conn, int hdr_len)
{
    struct tcb *tcb = &conn->tcb;
    struct tcp_opt_info opt_info = {
        .send_ts_ok = 1,
        .tsval = tcp_ts_now(s_tcp_ts_mul),
        .tsecr = tcb->ts_recent,
    };

    tcp_header_init(tcphdr, dpdk_be_to_cpu_16(tcphdr->dst_port), dpdk_be_to_cpu_16(tcphdr->src_port),
                    tcb->snd.una, tcb->rcv.nxt, TCP_F_RST_ACK, TCP_HDR_WIN, hdr_len);
    tcp_option_set((uint8_t *)(tcphdr + 1), &opt_info);
    _tcp4_pktmbuf_verify(m, tcphdr, DPDK_HEADROOM(m)->l3);
}

static INLINE void _tcp4_rst_ts_reply(struct dpdk_mbuf *m, struct tcp_conn *conn)
{
    int l3_payload = 0;
    int tcp_hdr_len = 0;
    uint16_t total_len = 0;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(m)->l4;

    tcp_hdr_len = TCP_HDR_MIN + TCP_OPTION_TS_LEN;
    tcp_hdr_len = UTIL_ALIGN_UP(tcp_hdr_len, 4);

    l3_payload = IP4_HDR_MIN + tcp_hdr_len;
    total_len = L2_MAC_HDR_MIN + l3_payload;

    dpdk_pktmbuf_set_len(m, L2_MAC_HDR_MIN, IP4_HDR_MIN, tcp_hdr_len, total_len);
    l2_pktmbuf_repay(m);
    ip4_pktmbuf_replay(m, IPPROTO_TCP, (uint16_t) l3_payload);
    _tcp4_pktmbuf_rst_ack(m, tcphdr, conn, tcp_hdr_len);
}

static INLINE void _tcp4_rst_reply(struct dpdk_mbuf *m, uint32_t seq, uint32_t ack)
{
    struct dpdk_ip4_hdr *ip4hdr = DPDK_HEADROOM(m)->l3;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(m)->l4;

    dpdk_pktmbuf_set_len(m, L2_MAC_HDR_MIN, IP4_HDR_MIN, TCP_HDR_MIN, TCP_PKTMBUF_MIN);

    l2_pktmbuf_repay(m);
    ip4_pktmbuf_replay(m, IPPROTO_TCP, dpdk_ip4_header_len(ip4hdr) + tcp_header_len(tcphdr));

    tcp_header_init(tcphdr, dpdk_be_to_cpu_16(tcphdr->dst_port), dpdk_be_to_cpu_16(tcphdr->src_port), seq, ack,
        ((ack != 0) ? TCP_F_RST_ACK : TCP_F_RST), 0, TCP_HDR_MIN);
    _tcp4_pktmbuf_verify(m, tcphdr, ip4hdr);
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
    return (irs + tcp_ts_now(s_tcp_ts_mul));
}

static INLINE void _tcp4_tcb_init(struct tcb *tcb, struct dpdk_tcp_hdr *tcphdr, struct dpdk_ip4_hdr *ip4hdr)
{
    uint32_t iss = _tcp4_irs_get(tcphdr, ip4hdr);

    tcb->snd.iss = iss;
    tcb->snd.una = iss;
    tcb->snd.nxt = iss + 1;
    tcb->send_win_shift = TCP_OPTION_WIN_SCALE;
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
    tcb->ts_recent = opt_info.tsval;
    tcb->mss = MIN(((opt_info.mss != 0) ? opt_info.mss : TCP_IP4_MSS_DEFAULT), TCP_IP4_MSS);

    tcb->rcv.irs = dpdk_be_to_cpu_32(tcphdr->sent_seq);
    tcb->rcv.nxt = tcb->rcv.irs + 1;
    tcb->rcv.wnd = ((uint32_t)dpdk_be_to_cpu_16(tcphdr->rx_win)) << opt_info.scale;
    tcb->rcv.end = tcb->rcv.nxt + tcb->rcv.wnd;

    return 0;
}

static INLINE void _tcp4_pktmbuf_syn_ack(struct dpdk_mbuf *m, struct dpdk_tcp_hdr *tcphdr, struct tcp_conn *conn, int hdr_len)
{
    struct tcb *tcb = &conn->tcb;
    struct tcp_opt_info opt_info = {
        .mss = TCP_IP4_MSS,
        .scale = TCP_WIN_SCALE,
        .send_ts_ok = tcb->send_ts_ok,
        .tsval = tcp_ts_now(s_tcp_ts_mul),
        .tsecr = tcb->ts_recent,
    };

    tcp_header_init(tcphdr, dpdk_be_to_cpu_16(tcphdr->dst_port), dpdk_be_to_cpu_16(tcphdr->src_port),
                    tcb->snd.una, tcb->rcv.nxt, TCP_F_SYN_ACK, TCP_HDR_WIN, hdr_len);
    tcp_option_syn_set((uint8_t *)(tcphdr + 1), &opt_info);
    _tcp4_pktmbuf_verify(m, DPDK_HEADROOM(m)->l4, DPDK_HEADROOM(m)->l3);
}

static INLINE void _tcp4_make_syn_ack(struct dpdk_mbuf *m, struct tcp_conn *conn)
{
    int total_len = 0;
    int l3_payload = 0;
    int tcp_hdr_len = 0;
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(m)->l4;

    tcp_hdr_len += TCP_HDR_MIN;
    tcp_hdr_len += TCP_OPTION_MSS_LEN;
    tcp_hdr_len += TCP_OPTION_SCALE_LEN;
    tcp_hdr_len += (conn->tcb.send_ts_ok ? TCP_OPTION_TS_LEN : 0);
    tcp_hdr_len = UTIL_ALIGN_UP(tcp_hdr_len, 4);

    l3_payload = IP4_HDR_MIN + tcp_hdr_len;
    total_len = L2_MAC_HDR_MIN + l3_payload;

    dpdk_pktmbuf_set_len(m, L2_MAC_HDR_MIN, IP4_HDR_MIN, tcp_hdr_len, total_len);

    l2_pktmbuf_repay(m);
    ip4_pktmbuf_replay(m, IPPROTO_TCP, (uint16_t)l3_payload);
    _tcp4_pktmbuf_syn_ack(m, tcphdr, conn, tcp_hdr_len);
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

    _tcp4_tcb_init(&conn->tcb, tcphdr, ip4hdr);
    _tcp4_make_syn_ack(m, conn);

    return 0;
}

static void _tcp4_vs_process(struct vserver4_kv_blk *vs4_blk)
{
    int ret = 0;
    int count = vs4_blk->count;
    struct vserver4 *v4 = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    uint64_t result = vs4_blk->result;

    for (int i = 0; i < count; i++) {
        mbuf = vs4_blk->mbufs[i];

        if (dpdk_bit_test_u64(result, i)) {
            v4 = vs4_blk->data[i];
            ret = _tcp4_conn_create(v4, mbuf, vs4_blk->tuple[i], vs4_blk->data[i]);
            if (UNLIKELY(ret != 0)) {
                pktmbuf_drop(mbuf);
            }
        } else {
            ret = _tcp4_state_process(mbuf, NULL);
            if (UNLIKELY(ret != 0)) {
                pktmbuf_drop(mbuf);
            }
        }
    }
}

static INLINE void _tcp4_conn_process(struct tcp4_lookup_blk *blk)
{
    int n = 0;
    struct vserver4_kv_blk *vs4_blk = sp_vs4_blk;

    n = _tcp4_conn_dispatch(vs4_blk, blk);
    if (n == 0) {
        return;
    }

    /*
     * For packets that do not find a matching session entry,
     * proceed to the next step to look up the VSERVER and create a new session entry.
     */
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

void *tcp4_thread_ops_get(void)
{
    return &s_tcp_ops;
}

int tcp4_thread_resource_init(void)
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
        blk->keys[i] = (struct tcp4_tuple *)&blk->tuples[i];
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

    v4_kv_blk->tuple = (void **)blk->keys;
    sp_vs4_blk = v4_kv_blk;
    s_tcp_ts_mul = tcp_ts_init();

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