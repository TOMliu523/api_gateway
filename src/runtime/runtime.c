/*****************************************************************************
 * filename: runtime.c
 * function:
 * description:
 ****************************************************************************/

#define _GNU_SOURCE
#include <pthread.h>

#include "top.h"
#include "common.h"

void *runtime_startup(void *arg)
{
    struct context *context = (struct context *)arg;

    pthread_setname_np(pthread_self(), "RUNTIME");
    check_and_wait(context->root.dataplane, context->info.cpu_count);

    for (;;) {

    }

    pthread_exit(NULL);
}