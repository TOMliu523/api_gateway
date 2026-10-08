/*****************************************************************************
 * filename: dataplane.c
 * function:
 * description: Worker Entry Point
 ****************************************************************************/

#define _GNU_SOURCE
#include <pthread.h>

#include "top.h"

static int _dp_init(struct data_root *root)
{
    return 0;
}

int dp_startup(void *arg)
{
    struct data_root *root = (struct data_root *) arg;

    _dp_init(root);

    for (;;) {

    }

    return 0;
}