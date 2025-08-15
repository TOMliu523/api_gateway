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
    uint32_t off_time;
    uint32_t off_time_ms;

    uint16_t arp_cpu_id;
};

static __thread struct timer_context s_context;

int timer_init(void *arg)
{
    struct timer_context *context = &s_context;

    context->root = arg;
    context->off_time = 0;
    context->arp_cpu_id = context->root->hw_info.cpu_count / 2;

    return 0;
}

void timer_check(void)
{
    struct timer_context *context = &s_context;

    if (tlv_thread_id == context->arp_cpu_id && tlv_dp->off_time > context->off_time) {
        l3_refresh();
    }

    if (tlv_dp->off_time > context->off_time && dpdk_ip_mbuf_need_recall(tlv_dp->frag_handle)) {
        dpdk_ip_mbuf_recall(tlv_dp->frag_handle, tlv_dp->timer_cycles);
    }

    if (context->off_time_ms + 10 <= tlv_dp->off_time_ms) {
        context->off_time_ms = tlv_dp->off_time_ms;
    }

    context->off_time = tlv_dp->off_time;
}