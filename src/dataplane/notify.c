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

static __thread struct root *root;

int notify_init(void *arg)
{
    root = arg;

    return 0;
}

static INLINE void _notify_headroom_copy(struct dpdk_mbuf *mbuf, struct dpdk_mbuf *one)
{
    struct dpdk_headroom *src = &((struct dpdk_data *)one)->headroom;
    struct dpdk_headroom *dst = &((struct dpdk_data *)mbuf)->headroom;

    dst->type = src->type;
    dst->target = src->target;
}

static void _notify_other_thread(void)
{
    int count = 0;
    int cpu_count = 0;
    int drop_count = 0;
    struct dpdk_mbuf *one = NULL;
    struct dpdk_mbuf *mbuf = NULL;

    count = tlv_notify->count;
    cpu_count = root->hw_info.cpu_count;
    for (int i = 0; i < cpu_count; i++) {
        int j = 0;
        struct dataplane *other = root->dpdk_thread[i];

        if (i == tlv_thread_id) {
            continue;
        }

        for (j = 0; j < count; j++) {
            one = tlv_notify->data[j];
            mbuf = dpdk_pktmbuf_clone(one, other->pktmbuf_pool);
            if (UNLIKELY(mbuf == NULL)) {
                LOG_ERROR("Notify CPU(%d) failure", i);
                dpdk_pktmbuf_push(tlv_cache->data, j);
                break;
            }

            mbuf->port = one->port;
            _notify_headroom_copy(mbuf, one);

            tlv_cache->data[j] = mbuf;
        }

        if (LIKELY(j == count)) {
            dpdk_ring_mp_push(other->notice_ring, (void *const *)tlv_cache->data, count);
        } else {
            break;
        }
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
        case PKT_MBUF_NDP:
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