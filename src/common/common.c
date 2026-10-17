/*****************************************************************************
 * filename: common.c
 * function:
 * description:
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "macro.h"
#include "atomic.h"
#include "common.h"

int alloc_align_system(void **data, size_t size)
{
    int ret = 0;

    if (data == NULL || size == 0) {
        return -1;
    }

    ret = posix_memalign(data, CACHE_LINE, size);
    if (ret != 0 || *data == NULL) {
        return -1;
    }

    return 0;
}

void check_and_wait(void *array[], int count)
{
    for (;;) {
        int n = 0;

        for (int i = 0; i < count; i++) {
            if (atomic_load(&array[i]) == NULL) {
                break;
            }

            n += 1;
        }

        if (n == count) {
            return;
        }
    }
}