 /*****************************************************************************
 * filename: notify.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "type.h"
#include "dpdk_core.h"
#include "dpdk_common.h"

static __thread struct root *root;

int notify_init(void *arg)
{
    root = arg;

    return 0;
}

static void _notify_other_thread_arp(void)
{
    int count = 0;
    int cpu_count = 0;
    int drop_count = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_headroom *headroom = NULL;

    count = notify_mbuf->count;
    drop_count = drop_mbuf->count;

    for (int i = 0; i < count; i++) {
        mbuf = notify_mbuf->data[i];
        headroom = &((struct dpdk_data *)mbuf)->headroom;
        headroom->type = PKT_MBUF_ARP;

        drop_mbuf->data[drop_count++] = mbuf;
    }

    drop_mbuf->count = drop_count;

    cpu_count = root->hw_info.cpu_count;
    for (int i = 0; i < cpu_count; i++) {
        struct dataplane *other = root->dpdk_thread[i];

        if (i == dp->cpu_id) {
            continue;
        }

        for (int j = 0; j < count; j++) {
            mbuf = dpdk_pktmbuf_clone(notify_mbuf->data[j], other->pktmbuf_pool);
            if (UNLIKELY(mbuf == NULL)) {
                LOG_ERROR("Notify CPU(%d) failure", i);
                dpdk_pktmbuf_push(cache_mbuf->data, j);
                break;
            }

            ((struct dpdk_data *)mbuf)->headroom.type = PKT_MBUF_ARP;
            cache_mbuf->data[j] = mbuf;
        }

        dpdk_ring_mp_push(other->notice_ring, (void *const *)cache_mbuf->data, count);
    }
}

static INLINE void _notify_accept_and_handle(void)
{
    unsigned count = 0;
    struct pkt_tx *tx = NULL;
    enum PKT_MBUF_TYPE type = 0;
    struct dpdk_mbuf *mbuf = NULL;
    static __thread void *mbufs[MBUF_NOTIFY_MAX] = {NULL};

    count = dpdk_ring_sc_pop(dp->notice_ring, mbufs, ARR_NUMS(mbufs));
    for (unsigned i = 0; i < count; i++) {
        mbuf = mbufs[i];
        type = ((struct dpdk_data *)mbuf)->headroom.type;

        switch (type) {
        case PKT_MBUF_GARP: // 0 thread
            tx = &tx_mbuf[mbuf->port];
            tx->data[tx->count++] = mbuf;
            break;
        case PKT_MBUF_ARP:
            l2_arp_update_or_create(mbuf);
            drop_mbuf->data[drop_mbuf->count++] = mbuf;
            break;
        default:
            LOG_ERROR("Not support type(%d)", type);
            break;
        }
    }
}

void notify_do(void)
{
    if (notify_mbuf->count != 0) {
        _notify_other_thread_arp();
        notify_mbuf->count = 0;
    }

    _notify_accept_and_handle();
}