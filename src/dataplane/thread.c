/************************************************
 * filename: thread.c
 * function:
 * description:
 ***********************************************/

#include <time.h>
#include <unistd.h>

#include "dataplane.h"

int dp_startup(void *arg)
{
    // TODO init

    for (;;) {
        sleep(1);
    }

    // TODO fini

    return 0;
}