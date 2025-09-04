 /*****************************************************************************
 * filename: notify.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "ip6.h"
#include "type.h"
#include "dpdk_core.h"
#include "dpdk_common.h"

static __thread uint16_t s_cpu_count;
static __thread struct root *s_root;

int notify_init(void *arg)
{
    s_root = arg;
    s_cpu_count = s_root->hw_info.cpu_count;

    return 0;
}

static void _notify_other_thread(void)
{
    int count = 0;
    int drop_count = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dataplane *other = NULL;

    count = tlv_notify->count;

    for (int i = 0; i < count; i++) {
        dpdk_mbuf_refcnt_set(tlv_notify->data[i], s_cpu_count);
    }

    for (int i = 0; i < s_cpu_count; i++) {
        if (i == tlv_thread_id) {
            continue;
        }

        other = s_root->dpdk_thread[i];
        dpdk_ring_mp_push(other->notice_ring, (void *const *)tlv_notify->data, count);
    }

    drop_count = tlv_drop->count;

    UNROLL_LOOP_8(i, count, {
        mbuf = tlv_notify->data[i];
        tlv_drop->data[drop_count++] = mbuf;
    });

    tlv_drop->count = drop_count;
}

static INLINE void _notify_accept_and_handle(void)
{
    unsigned count = 0;
    struct pkt_tx *tx = NULL;
    enum PKT_MBUF_TYPE type = 0;
    struct dpdk_mbuf *mbuf = NULL;
    static __thread void *mbufs[MBUF_NOTIFY_MAX] = {NULL};

    count = dpdk_ring_sc_pop(tlv_dp->notice_ring, mbufs, ARR_NUMS(mbufs));
    for (unsigned i = 0; i < count; i++) {
        mbuf = mbufs[i];
        type = DPDK_HEADROOM(mbuf)->type;

        switch (type) {
        case PKT_MBUF_GARP:
        case PKT_MBUF_NDP_AD:
            tx = &tlv_tx[mbuf->port];
            tx->data[tx->count++] = mbuf;
            break;
        case PKT_MBUF_ARP:
            l2_arp_update_or_create(mbuf);
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        case PKT_MBUF_NDP_SRC:
        case PKT_MBUF_NDP_TARGET:
            ip6_ndp_update_or_create(mbuf);
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        default:
            LOG_ERROR("Not support type(%d)", type);
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        }
    }
}

void notify_do(void)
{
    if (tlv_notify->count != 0) {
        _notify_other_thread();
        tlv_notify->count = 0;
    }

    _notify_accept_and_handle();
}