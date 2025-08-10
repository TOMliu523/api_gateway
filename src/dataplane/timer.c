/*****************************************************************************
 * filename: timer.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "l3.h"
#include "type.h"
#include "timer.h"
#include "dpdk_ip.h"

struct timer_context {
    struct root *root;
    struct timer_arp {
        uint16_t arp_cpu_id;
        uint32_t off_time;
        uint32_t off_time_ms;
    } ta;
};

static __thread struct timer_context s_context;

int timer_init(void *arg)
{
    struct timer_context *context = &s_context;

    context->root = arg;
    context->ta.arp_cpu_id = context->root->hw_info.cpu_count / 2;
    context->ta.off_time = 0;

    return 0;
}

void timer_check(void)
{
    struct timer_arp *ta = &s_context.ta;

    if (tlv_thread_id == ta->arp_cpu_id && tlv_dp->off_time > ta->off_time) {
        l3_refresh();
    }

    if (tlv_dp->off_time > ta->off_time && dpdk_ip_mbuf_need_recall(tlv_dp->frag_handle)) {
        dpdk_ip_mbuf_recall(tlv_dp->frag_handle, tlv_dp->timer_cycles);
    }

    ta->off_time = tlv_dp->off_time;
}