 /*****************************************************************************
 * filename: tcp.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __TCP_H__
#define __TCP_H__

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

enum TCP_STATE {
    TCP_ESTABLISHED = 1,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
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

struct tcp_send {
    uint32_t una;
    uint32_t nxt;
    uint32_t wnd;
    uint32_t wl1;
    uint32_t wl2;
    uint32_t iss;
};

struct tcp_recv {
    uint32_t nxt;
    uint32_t wnd;
    uint32_t irs;
};

struct tcb {
    struct tcp_send snd;
    struct tcp_recv rcv;
};

struct tcp_conn {
    uint8_t side; // CLIENTSIDE SERVERSIDE
    uint8_t state; // tcp state
    uint8_t af; // AF_INET AF_INET6
    uint8_t port; // interface
    uint32_t vs_id : 14; // max 16383
    uint32_t rs_id : 18; // max 262143
    struct tcb tcb; // Transmission Control Block
    uint32_t expire_time; // Since system startup to the current time
    dpdk_atomic32_t refcnt;
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

extern void tcp_state_process(struct dpdk_mbuf *, struct tcp_conn *);

static INLINE int tcp_header_len(const struct dpdk_tcp_hdr *tcp_hdr)
{
    return ((tcp_hdr->data_off >> 4) << 2);
}

#endif // __TCP_H__