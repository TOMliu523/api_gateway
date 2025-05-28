/************************************************
 * filename: main.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_errno.h>
#include <rte_cycles.h>
#include <rte_random.h>

#include "log.h"
#include "api.h"
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
                LOG_ERROR("create %s failure: %s", RUN_LOCK_FILE, strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                continue;
            }
        } else {
            s_lock_fd = open(RUN_LOCK_FILE, O_RDWR);
            if (s_lock_fd >= 0) {
                break;
            } else if (s_lock_fd < 0 && errno != ENOENT) {
                LOG_ERROR("open %s failure: %s.", RUN_LOCK_FILE, strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                continue;
            }
        }
    }

    nbytes = snprintf(buffer, sizeof(buffer), "process startup time: %lu\nprocess number: %u", time(NULL), getpid());
    if (nbytes < 0) {
        LOG_ERROR("snprintf failure: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = nbytes;
    lock.l_len = 10;

    ret = fcntl(s_lock_fd, F_SETLK, &lock);
    if (ret < 0) {
        LOG_ERROR("lock %s failure: %s", RUN_LOCK_FILE, strerror(errno));
        exit(EXIT_FAILURE);
    }

    write(s_lock_fd, buffer, nbytes);
}

static int worker_task(void *arg)
{
    LOG_INFO("thread number: %d", rte_lcore_id());

    for (;;) {
#include <time.h>
        sleep(1);
    }
}

static void signal_process(void)
{
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

int main(int argc, char *argv[])
{
    int i = 0;
    int ret = 0;
    pthread_t tid = {0};

    ret = daemon(0, 0);
    if (ret != 0) {
        LOG_ERROR("daemon failure: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    single_instance();
    signal_process();

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure. %s ", rte_strerror(rte_errno));
        return EXIT_FAILURE;
    }

    argc -= ret;
    argv += ret;

    rte_srand(rte_rdtsc());

    ret = pthread_create(&tid, NULL, api_startup, NULL);
    if (ret != 0) {
        LOG_ERROR("startup api thread failure: %s", strerror(ret));
        return EXIT_FAILURE;
    }

    LOG_INFO("STARTUP DPDK THREAD.");

    RTE_LCORE_FOREACH_WORKER(i) {
        rte_eal_remote_launch(worker_task, NULL, i);
    }
    worker_task(NULL);

    return EXIT_SUCCESS;
}
