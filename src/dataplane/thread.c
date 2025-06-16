/************************************************
 * filename: thread.c
 * function:
 * description:
 ***********************************************/

#include <stdio.h>
#include <unistd.h>

#include "log.h"
#include "type.h"
#include "dpdk_init.h"
#include "dataplane.h"

static INLINE void _dp_init(void *arg)
{
    char thread_name[64] = "";
    struct dataplane *dp = arg;

    snprintf(thread_name, sizeof(thread_name), "DATAPLANE%x%X", dp->socket_id, dp->index);
    dpdk_thread_set_name(thread_name);
}

int dp_startup(void *arg)
{
    // TODO init
    _dp_init(arg);

    for (;;) {
        sleep(1);
    }

    // TODO fini

    return 0;
}