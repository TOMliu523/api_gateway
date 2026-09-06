/*****************************************************************************
 * filename: main.c
 * function:
 * description: Application Entry Point
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

int main(int argc, char *argv[])
{
    int ret = 0;

    ret = daemon(0, 0);
    if (ret < 0) {
        LOG_ERROR("daemon failure: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}