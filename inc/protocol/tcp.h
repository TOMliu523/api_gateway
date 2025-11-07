 /*****************************************************************************
 * filename: tcp.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __TCP_H__
#define __TCP_H__

#include "util.h"
#include "dpdk_ip6.h"
#include "dpdk_tcp.h"
#include "dpdk_atomic.h"

#include "list.h"

// The following focuses on the state machine–related flags.
#define TCP_F_MASK (DPDK_TCP_FIN_F | DPDK_TCP_SYN_F | DPDK_TCP_RST_F | DPDK_TCP_ACK_F)

#define TCP_F_NONE 0

#define TCP_F_FIN DPDK_TCP_FIN_F
#define TCP_F_SYN DPDK_TCP_SYN_F
#define TCP_F_RST DPDK_TCP_RST_F
#define TCP_F_ACK DPDK_TCP_ACK_F

// Two-flag combinations
#define TCP_F_FIN_SYN (DPDK_TCP_FIN_F | DPDK_TCP_SYN_F)
#define TCP_F_FIN_RST (DPDK_TCP_FIN_F | DPDK_TCP_RST_F)
#define TCP_F_FIN_ACK (DPDK_TCP_FIN_F | DPDK_TCP_ACK_F)
#define TCP_F_SYN_RST (DPDK_TCP_SYN_F | DPDK_TCP_RST_F)
#define TCP_F_SYN_ACK (DPDK_TCP_SYN_F | DPDK_TCP_ACK_F)
#define TCP_F_RST_ACK (DPDK_TCP_RST_F | DPDK_TCP_ACK_F)

// Three-flag combinations
#define TCP_F_FIN_SYN_RST (DPDK_TCP_FIN_F | DPDK_TCP_SYN_F | DPDK_TCP_RST_F)
#define TCP_F_FIN_SYN_ACK (DPDK_TCP_FIN_F | DPDK_TCP_SYN_F | DPDK_TCP_ACK_F)
#define TCP_F_FIN_RST_ACK (DPDK_TCP_FIN_F | DPDK_TCP_RST_F | DPDK_TCP_ACK_F)
#define TCP_F_SYN_RST_ACK (DPDK_TCP_SYN_F | DPDK_TCP_RST_F | DPDK_TCP_ACK_F)

// All flags set (4 bits)
#define TCP_F_ALL (DPDK_TCP_FIN_F | DPDK_TCP_SYN_F | DPDK_TCP_RST_F | DPDK_TCP_ACK_F)

#define TCP_CLIENTSIDE 0
#define TCP_SERVERSIDE 1

#define TCP_RTX_R1 6
#define TCP_RTX_R2 10

#define TCP_OPTION_LEN_MAX 40
#define TCP_HDR_WIN (32768)
#define TCP_WIN_SCALE (6)
#define TCP_WIN_SIZE_DEFAULT (TCP_HDR_WIN * (1 << TCP_WIN_SCALE))

enum TCP_OPTION {
    TCP_OPTION_EOL,
    TCP_OPTION_NOP,
    TCP_OPTION_MSS,
    TCP_OPTION_WIN_SCALE,
    TCP_OPTION_TIMESTAMP = 8,
};

enum TCP_STATE {
    TCP_ESTABLISHED = 1,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVE,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_CLOSE,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_LISTEN,
    TCP_CLOSING,
};

typedef int (*tcp_cb_t)(void *);

struct tcp_opt_info {
    uint16_t mss;
    uint8_t scale;
    uint8_t send_ts_ok;
    union {
        uint64_t tsopt;
        struct {
            uint64_t tsval : 32;
            uint64_t tsecr : 32;
        };
    };
};

struct tcp4_tuple {
    uint32_t sip;
    uint32_t dip;
    uint16_t sport;
    uint16_t dport;
    uint32_t version;
};

struct tcp6_tuple {
    struct dpdk_ip6_addr sip;
    struct dpdk_ip6_addr dip;
    uint16_t sport;
    uint16_t dport;
    uint32_t version;
};

struct send_win {
    uint32_t una;
    uint32_t nxt;
    uint32_t wnd;
    uint32_t wl1;
    uint32_t wl2;
    uint32_t iss;
};

struct recv_win {
    uint32_t nxt;
    uint32_t wnd;
    uint32_t irs;
};

struct rtt_stats {
    uint32_t srtt;
    uint32_t rttval;
    uint32_t rtt_stamp;
    uint32_t rto;
};

struct tcb {
    uint8_t recv_win_shift : 4;
    uint8_t send_win_shift : 3;
    uint8_t send_ts_ok : 1;
    uint8_t retry;
    uint16_t mss;
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t ts_recent;
    uint32_t last_ack_sent;
    struct rtt_stats rtt;
    struct send_win snd;
    struct recv_win rcv;
};

struct tcp_conn {
    uint16_t side : 1; // CLIENTSIDE SERVERSIDE
    uint16_t state : 4; // tcp state
    uint16_t af : 5; // AF_INET AF_INET6
    uint16_t port : 6; // interface
    dpdk_atomic16_t refcnt;
    uint32_t vs_id : 14; // max 16383
    uint32_t rs_id : 18; // max 262143
    uint32_t expire_time; // Since system startup to the current time
    struct tcb tcb; // Transmission Control Block
    void *rcv_queue;
    void *snd_queue;
    void *resnd_queue;
    struct tcp_tuple *tuple; // Convert to tcp4_tuple or tcp6_tuple according to the AF type.
    union {
        struct tcp_conn *proxy;
        struct http *http;
        struct ssl *ssl;
        struct http2 *http2;
    };

    /*
     * Used to register a callback for upper-layer modules.
     * Triggered upon specific events, serving as a unified entry point.
     */
    tcp_cb_t cb;
    struct list_head node;
} ALIGN_CACHE_LINE;

struct tcp4_lookup_blk {
    int count;
    uint64_t resutl;
    struct tcp4_tuple tuples[DP_BATCH_MAX];
    struct tcp4_tuple *keys[DP_BATCH_MAX];
    struct tcp_conn *conns[DP_BATCH_MAX];
    void **mbufs;
};

struct tcp6_lookup_blk {
    int count;
    uint64_t result;
    struct tcp6_tuple tuple[DP_BATCH_MAX];
    struct tcp4_tuple *keys[DP_BATCH_MAX];
    struct tcp_conn *conn[DP_BATCH_MAX];
    void **mbufs;
};

extern int tcp_thread_create(void);
extern void tcp_thread_destroy(void);
extern void tcp_conn_lookup(struct tcp4_lookup_blk *);

extern int tcp4_thread_create(void);
extern void tcp4_thread_destroy(void);
extern void tcp4_process(void *[], int);
extern void tcp6_process(void *[], int);

extern void tcp_state_process(struct dpdk_mbuf *, struct tcp_conn *, void *);
extern void *tcp_conn_client_create(int, uint16_t, uint32_t, struct tcp_tuple *);

static INLINE int tcp_header_len(const struct dpdk_tcp_hdr *tcp_hdr)
{
    return ((tcp_hdr->data_off >> 4) << 2);
}

static INLINE void tcp_header_init(struct dpdk_tcp_hdr *tcphdr, uint16_t sport, uint16_t dport, uint32_t seq,
                                   uint32_t ack, uint8_t flags, uint16_t win, uint8_t hdr_bytes)
{
    tcphdr->src_port = dpdk_cpu_to_be_16(sport);
    tcphdr->dst_port = dpdk_cpu_to_be_16(dport);
    tcphdr->sent_seq = dpdk_cpu_to_be_32(seq);
    tcphdr->recv_ack = dpdk_cpu_to_be_32(ack);
    tcphdr->data_off = (hdr_bytes >> 2) << 4;
    tcphdr->tcp_flags = flags;
    tcphdr->rx_win = dpdk_cpu_to_be_16(win);
    tcphdr->cksum = 0;
    tcphdr->tcp_urp = 0;
}

static INLINE int __tcp_option_timestamp_set(uint8_t data[], uint64_t tsopt)
{
    int n = 0;
    uint64_t be64 = 0;

    if (tsopt == 0) {
        return 0;
    }

    // Avoid unaligned access penalty
    be64 = dpdk_cpu_to_be_64(tsopt);

    data[n++] = TCP_OPTION_TIMESTAMP;
    data[n++] = 10;
    dpdk_memcpy(&data[n], &be64, sizeof(be64));
    n += 8;

    return n;
}

static INLINE int tcp_option_set(uint8_t data[], const struct tcp_opt_info *info)
{
    int n = 0;
    n = __tcp_option_timestamp_set(data, info->tsopt);
    switch (n & 3) {
    case 1: data[n++] = 0; FALLTHROUGH;
    case 2: data[n++] = 0; FALLTHROUGH;
    case 3: data[n++] = 0; FALLTHROUGH;
    default: return n;
    }
}

static INLINE int tcp_syn_option_set(uint8_t data[], const struct tcp_opt_info *info)
{
    int n = 0;

    if (info->mss != 0) {
        data[n++] = TCP_OPTION_MSS;
        data[n++] = 4,
        *(uint16_t *)&data[n] = dpdk_cpu_to_be_16(info->mss);
        n += 2;
    }

    // A TCP SHOULD send this option, even if its own scale factor is 1 (i.e., shift.cnt = 0).
    data[n++] = TCP_OPTION_WIN_SCALE;
    data[n++] = 3;
    data[n++] = info->scale;

    n += __tcp_option_timestamp_set(&data[n], info->tsopt);
    switch (n & 3) {
    case 1: data[n++] = 0; FALLTHROUGH;
    case 2: data[n++] = 0; FALLTHROUGH;
    case 3: data[n++] = 0; FALLTHROUGH;
    default: return n;
    }
}

static INLINE int tcp_option_get(struct tcp_opt_info *info, uint8_t data[], int len)
{
    uint8_t option_len = 0;
    uint8_t *start = data;
    uint8_t *end = data + len;

    while (start < end) {
        switch (*start) {
        case TCP_OPTION_EOL: return 0;
        case TCP_OPTION_NOP: start += 1; break;
        case TCP_OPTION_MSS:
            if (UNLIKELY(start + 3 >= end || *++start != 4)) {
                return -1;
            }

            start += 1;
            dpdk_memcpy(&info->mss, start, sizeof(info->mss));
            info->mss = dpdk_be_to_cpu_16(info->mss);
            start += 2;
            break;
        case TCP_OPTION_WIN_SCALE:
            if (UNLIKELY(start + 2 >= end || *++start != 3)) {
                return -1;
            }

            start += 1;
            info->scale = (*start <= 14) ? (*start) : 14;
            start += 1;
            break;
        case TCP_OPTION_TIMESTAMP:
            if (UNLIKELY(start + 9 >= end || *++start != 10)) {
                return -1;
            }

            start += 1;
            dpdk_memcpy(&info->tsopt, start, sizeof(info->tsopt));
            info->tsopt = dpdk_be_to_cpu_64(info->tsopt);
            start += 8;
            info->send_ts_ok = 1;
            break;
        default:
            if (UNLIKELY(start + 1 >= end)) {
                return -1;
            }

            start += 1;
            option_len = *start;
            if (UNLIKELY(start + option_len - 1 >= end)) {
                return -1;
            }

            start += option_len - 1;
            break;
        }
    }

    return 0;
}

/*
 * NOTE:
 * Sequence number comparisons MUST use these helper functions
 * (tcp_seq_before, tcp_seq_after, tcp_seq_leq, tcp_seq_geq, tcp_seq_between)
 * instead of direct integer comparison.
 * This ensures correct handling of 32-bit sequence number wrap-around.
 */

static INLINE bool tcp_seq_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static INLINE bool tcp_seq_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static INLINE bool tcp_seq_leq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

static INLINE bool tcp_seq_geq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static INLINE bool tcp_seq_between(uint32_t x, uint32_t start, uint32_t end)
{
    return tcp_seq_geq(x, start) && tcp_seq_before(x, end);
}

static INLINE bool tcp_ts_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static INLINE bool tcp_ts_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

#endif // __TCP_H__