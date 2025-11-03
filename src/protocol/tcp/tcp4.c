/*****************************************************************************
 * filename: tcp4.c
 * function:
 * description:
 *****************************************************************************/

#include "log.h"
#include "tcp.h"
#include "type.h"
#include "vserver.h"
#include "dpdk_type.h"
#include "dpdk_bitops.h"

static __thread struct vserver4_kv_blk *sp_vs4_blk;
static __thread struct tcp4_lookup_blk *sp_tcp4_lookup_blk;

static INLINE bool _tcp_mbuf_cksum_verify(const struct dpdk_mbuf *mbuf, const struct dpdk_tcp_hdr *tcphdr)
{
    switch (mbuf->ol_flags & DPDK_TCP_RX_CKSUM_MASK) {
    case DPDK_TCP_RX_CKSUM_GOOD: return true;
    case DPDK_TCP_RX_CKSUM_BAD: return false;
    default: return dpdk_tcp4_mbuf_cksum_verify(mbuf, tcphdr);
    }

    return true;
}

static int _tcp4_validate_and_prepare(struct tcp4_lookup_blk *blk, void *data[], int count)
{
    int n = 0;
    int payload_len = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct dpdk_tcp_hdr *tcphdr = NULL;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ip4hdr = DPDK_HEADROOM(mbuf)->l3;
        tcphdr = DPDK_HEADROOM(mbuf)->l4;

        payload_len = dpdk_be_to_cpu_32(ip4hdr->total_length) - mbuf->l3_len - tcp_header_len(tcphdr);
        if (UNLIKELY(payload_len < 0)) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY(!_tcp_mbuf_cksum_verify(mbuf, tcphdr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
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
    struct dpdk_tcp_hdr *tcphdr = DPDK_HEADROOM(mbuf)->l4;

    if (LIKELY((tcphdr->tcp_flags & TCP_F_MASK) == TCP_F_SYN)) {
        return 0;
    }

    // TODO Check the timestamp, and discard packets whose expiration time is too long.

    return -1;
}

static INLINE int _tcp4_dispatch(struct vserver4_kv_blk *vs4_blk, struct tcp4_lookup_blk *blk)
{
    int n = 0;
    int ret = 0;
    int count = blk->count;
    uint64_t result = blk->resutl;
    struct dpdk_mbuf *mbuf = NULL;
    struct tcp4_tuple *tuple = NULL;

    for (int i = 0; i < count; i++) {
        if (dpdk_bit_test_u64(result, i)) {
            // TODO has tcp connection, entry tcp stack
        } else {
            mbuf = blk->mbufs[i];

            ret = _tcp4_header_syn_process(mbuf);
            if (UNLIKELY(ret != 0)) {
                tcp_state_process(mbuf, NULL);
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

static void _tcp4_vs_process(struct vserver4_kv_blk *vs4_blk)
{
    int ret = 0;
    int count = vs4_blk->count;
    // struct vserver_v4 *v4 = NULL;
    uint64_t result = vs4_blk->result;

    for (int i = 0; i < count; i++) {
        if (dpdk_bit_test_u64(result, i)) {
            // v4 = vs4_blk->data[i];
            // ret = tcp_conn_create(&v4->id);
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

    vs4_blk->mbufs = blk->mbufs;
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

    blk->mbufs = data;
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