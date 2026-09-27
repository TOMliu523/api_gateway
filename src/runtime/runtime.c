/*****************************************************************************
 * filename: runtime.c
 * function:
 * description:
 ****************************************************************************/

#define _GNU_SOURCE
#include <pthread.h>

void *runtime_startup(void *arg)
{
    pthread_setname_np(pthread_self(), "RUNTIME");

    for (;;);
    pthread_exit(NULL);
}