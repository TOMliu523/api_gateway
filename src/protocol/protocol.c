/*****************************************************************************
 * filename: protocol.c
 * function:
 * description:
 *****************************************************************************/

#include "l2.h"
#include "l3.h"
#include "log.h"
#include "type.h"
#include "protocol.h"
#include "dpdk_common.h"

void *protocol_create(int nic_count, void *arg)
{
    struct dataplane *dp = arg;
    struct proto_header *header = NULL;

    header = dpdk_malloc(sizeof(*header));
    if (UNLIKELY(header == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    memset(header, 0, sizeof(*header));

    header->mac = l2_thread_mac_create(nic_count);
    if (UNLIKELY(header->mac == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    header->at = l2_thread_arp_table_create(nic_count, dp->cpu_id, dp->hw_numa_id);
    if (UNLIKELY(header->at == NULL)) {
        LOG_ERROR("OOM.");
        goto _quit;
    }

    return header;

_quit:
    protocol_destroy(header);
    return NULL;
}

void protocol_destroy(void *arg)
{
    struct proto_header *header = arg;

    if (header == NULL) {
        return;
    }

    l2_thread_mac_destroy(header->mac);
    l2_thread_arp_table_destroy(header->at);
    l3_thread_ipv4_destroy(header->ipv4);
    dpdk_free(header);
}