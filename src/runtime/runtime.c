/*****************************************************************************
 * filename: runtime.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>

#include "type.h"
#include "atomic.h"
#include "runtime.h"

static void _runtime_init(void *arg)
{
}

static void _runtime_wait_dataplane(const struct root *root)
{
    for (; !atomic_load(&root->inited););
}

void *runtime_mgmt_startup(void *arg)
{
    _runtime_init(arg);
    _runtime_wait_dataplane(arg);

    for (;;) {

    }

    return NULL;
}