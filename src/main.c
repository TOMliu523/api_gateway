/************************************************
 * filename: main.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_random.h>

#include "log.h"

int main(int argc, char *argv[])
{
    int ret = 0;

    ret = daemon(0, 0);
    if (ret != 0) {
        fprintf(stderr, "daemon failure: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    ret = rte_eal_init(argc, argv);
    if (ret != 0) {
        LOG_ERROR("");
        return EXIT_FAILURE;
    }

    rte_srand(rte_rdtsc());



    return EXIT_SUCCESS;
}
