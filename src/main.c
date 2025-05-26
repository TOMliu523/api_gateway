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
#include <fcntl.h>
#include <sys/types.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_cycles.h>
#include <rte_random.h>

#include "log.h"
#include "macro.h"

#define RUN_LOCK_FILE "/run/lock/api_gateway.lock"

static int s_lock_fd;

static INLINE void single_instance(void)
{
    int ret = 0;
    off_t off = 0;
    int nbytes = 0;
    struct flock lock = {0};
    char buffer[BUFSIZ] = "";

    for (;;) {
        if (access(RUN_LOCK_FILE, F_OK) != 0) {
            s_lock_fd = open(RUN_LOCK_FILE, O_CREAT | O_EXCL | O_RDWR, 0666);
            if (s_lock_fd >= 0) {
                break;
            } else if (s_lock_fd < 0 && errno != EEXIST) {
                LOG_ERROR("create %s failure: %s\n", RUN_LOCK_FILE, strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                continue;
            }
        } else {
            s_lock_fd = open(RUN_LOCK_FILE, O_RDWR);
            if (s_lock_fd >= 0) {
                break;
            } else if (s_lock_fd < 0 && errno != ENOENT) {
                LOG_ERROR("open %s failure: %s.\n", RUN_LOCK_FILE, strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                continue;
            }
        }
    }

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = nbytes;
    lock.l_len = 10;

    ret = fcntl(s_lock_fd, F_SETLK, &lock);
    if (ret < 0) {
        LOG_ERROR("lock %s failure: %s\n", RUN_LOCK_FILE, strerror(errno));
        exit(EXIT_FAILURE);
    }

    nbytes = snprintf(buffer, sizeof(buffer), "process startup time: %lu\nprocess number: %u\n", time(NULL), getpid());
    if (nbytes < 0) {
        LOG_ERROR("snprintf failure: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    write(s_lock_fd, buffer, nbytes);
}

int main(int argc, char *argv[])
{
    int ret = 0;

    single_instance();

    ret = daemon(0, 0);
    if (ret != 0) {
        LOG_ERROR("daemon failure: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure. %s \n", rte_strerror(rte_errno));
        return EXIT_FAILURE;
    }

    argc -= ret;
    argv += ret;

    rte_srand(rte_rdtsc());

    LOG_INFO("SUCCESS.\n");

    // for (;;);



    return EXIT_SUCCESS;
}
