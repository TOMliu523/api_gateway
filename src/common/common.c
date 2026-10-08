/*****************************************************************************
 * filename: common.c
 * function:
 * description:
 *****************************************************************************/

#include "top.h"
#include "common.h"

void check_dataplane_all_startup(void *arg)
{
    struct context *context = arg;
    int cpu_count = context->info.cpu_count;
    struct data_root *root = &context->root;

    for (;;) {
        int count = 0;

        for (int i = 0; i < cpu_count; i++) {
            if (atomic_load(&root->dataplane[i]) == NULL) {
                break;
            }

            count += 1;
        }

        if (count == cpu_count) {
            return;
        }
    }
}