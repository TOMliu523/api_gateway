/*****************************************************************************
 * filename: runtime.c
 * function:
 * description:
 *****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>

#include "type.h"
#include "atomic.h"
#include "runtime.h"

static void _runtime_init(void *arg)
{
    pthread_setname_np(pthread_self(), "RUNTIME_STATE");
}

static void _runtime_wait_dataplane(const struct root *root)
{
    for (; !atomic_load(&root->inited););
}

void *runtime_state_startup(void *arg)
{
    _runtime_init(arg);
    _runtime_wait_dataplane(arg);

    for (;;) {

    }

    return NULL;
}