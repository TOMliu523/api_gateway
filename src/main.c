/*****************************************************************************
 * filename: main.c
 * function:
 * description: Application Entry Point
 ****************************************************************************/

#define _GNU_SOURCE
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <rte_lcore.h>
#include <rte_ethdev.h>

#include "log.h"
#include "type.h"
#include "macro.h"
#include "config.h"
#include "api_http.h"
#include "dpdk_init.h"

#define RUN_LOCK_FILE "/run/lock/api_gateway.lock"

static void _fd_cleanup(int *ptr)
{
    int fd = *ptr;

    if (fd >= 0) {
        close(fd);
    }
}

static void _signal_process(void)
{
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
}

static int _single_instance(const char *filename)
{
    int fd = -1;
    int ret = 0;
    int nbytes = 0;
    struct flock lock = {0};
    char buffer[BUFSIZ] = "";

    for (;;) {
        if (access(filename, F_OK) != 0) {
            fd = open(filename, O_CREAT | O_EXCL | O_RDWR, 0666);
            if (fd >= 0) {
                break;
            } else if (fd < 0 && errno != EEXIST) {
                LOG_ERROR("create %s failure: %s", filename, strerror(errno));
                return -1;
            } else {
                continue;
            }
        } else {
            fd = open(filename, O_RDWR);
            if (fd >= 0) {
                break;
            } else if (fd < 0 && errno != ENOENT) {
                LOG_ERROR("open %s failure: %s.", filename, strerror(errno));
                return -1;
            } else {
                continue;
            }
        }
    }

    nbytes = snprintf(buffer, sizeof(buffer), "process number: %u\nprocess startup time: %lu", getpid(), time(NULL));
    if (nbytes < 0) {
        LOG_ERROR("snprintf failure: %s", strerror(errno));
        goto _quit;
    }

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = nbytes;
    lock.l_len = 1;

    ret = fcntl(fd, F_SETLK, &lock);
    if (ret != 0) {
        LOG_ERROR("lock %s failure: %s", filename, strerror(errno));
        goto _quit;
    }

    nbytes = write(fd, buffer, nbytes);
    if (nbytes < 0) {
        LOG_ERROR("Failure write: %s.", strerror(errno));
        goto _quit;
    }

    LOG_INFO("Current process ID = %u", getpid());
    return fd;

_quit:
    close(fd);
    return -1;
}

static void _non_dpdk_thread_attr_fini(pthread_attr_t *attr)
{
    if (attr != NULL) {
        pthread_attr_destroy(attr);
    }
}

/*
 * Before dpdk_init() is called, the main thread runs on non-DPDK CPUs.
 * After dpdk_init(), the main thread is pinned to a DPDK CPU.
 *
 * Save the CPU affinity attributes before dpdk_init() so that
 * non-dataplane threads created later can inherit them. This prevents
 * those threads from competing with dataplane threads for CPU resources
 * and ensures that they run only on non-DPDK CPUs.
 */
static int _non_dpdk_thread_attr_init(pthread_attr_t *attr)
{
    int ret = 0;
    cpu_set_t set = {0};

    CPU_ZERO(&set);
    ret = sched_getaffinity(getpid(), sizeof(set), &set);
    if (ret < 0) {
        LOG_ERROR("Function(sched_getaffinity) failure: %s", strerror(errno));
        return -1;
    }

    ret = pthread_attr_init(attr);
    if (ret < 0) {
        LOG_ERROR("Function(pthread_attr_init) failure: %s", strerror(errno));
        return -1;
    }

    ret = pthread_attr_setaffinity_np(attr, sizeof(set), &set);
    if (ret < 0) {
        LOG_ERROR("Function(pthread_attr_setaffinity_np) failure: %s", strerror(errno));
        pthread_attr_destroy(attr);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    pthread_t tid = {0};
    struct context context = {0};
    AUTO_CLEANUP(_fd_cleanup) int fd = -1;
    AUTO_CLEANUP(_non_dpdk_thread_attr_fini) pthread_attr_t attr = {0};

    ret = daemon(0, 0);
    if (ret < 0) {
        LOG_ERROR("daemon failure: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    _signal_process();
    fd = _single_instance(RUN_LOCK_FILE);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    ret = _non_dpdk_thread_attr_init(&attr);
    if (ret < 0) {
        return EXIT_FAILURE;
    }

    ret = dpdk_init(argc, argv);
    if (ret < 0) {
        return EXIT_FAILURE;
    }

    argc -= ret;
    argv += ret;

    dpdk_hw_info_init(&context.info);
    ret = config_boot_load(&context.config);
    if (ret < 0) {
        goto _quit;
    }

    ret = pthread_create(&tid, &attr, api_http, &context);
    if (ret < 0) {
        LOG_ERROR("Function(pthread_create) failure: %s", strerror(-ret));
        goto _quit;
    }

    sleep(200);
    return EXIT_SUCCESS;

_quit:
    dpdk_fini(0);
    return EXIT_FAILURE;
}