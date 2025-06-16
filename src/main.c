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

#include "log.h"
#include "api.h"
#include "type.h"
#include "dpdk_init.h"
#include "dataplane.h"

#define RUN_LOCK_FILE "/run/lock/api_gateway.lock"

static struct root *s_root;

static int single_instance(const char *filename)
{
    int fd = -1;
    int ret = 0;
    off_t off = 0;
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
                exit(EXIT_FAILURE);
            } else {
                continue;
            }
        } else {
            fd = open(filename, O_RDWR);
            if (fd >= 0) {
                break;
            } else if (fd < 0 && errno != ENOENT) {
                LOG_ERROR("open %s failure: %s.", filename, strerror(errno));
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

    ret = fcntl(fd, F_SETLK, &lock);
    if (ret < 0) {
        LOG_ERROR("lock %s failure: %s", filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    write(fd, buffer, nbytes);
    LOG_INFO("Current process ID = %u", getpid());

    return fd;
}

static void signal_process(void)
{
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
}

static void root_fini(void)
{
    struct root *root = NULL;

    if (s_root == NULL) {
        return;
    }

    root = s_root;

    close(root->lock_fd);
    free(root);

    s_root = NULL;
}

static struct root *root_init(struct hw_info *info, int lock_fd, const char *lock_file)
{
    int ret = 0;
    size_t numa_size = 0;
    size_t total_size = 0;
    struct root *root = NULL;

    numa_size = info->numa_count * sizeof(struct numa_content);
    total_size = sizeof(struct root) + numa_size;
    ret = posix_memalign((void **)&root, CACHE_LINE, total_size);
    if (ret < 0) {
        LOG_ERROR("OOM");
        exit(EXIT_FAILURE);
    }

    memset(root, 0, total_size);

    root->lock_fd = lock_fd;
    root->lock_filename = lock_file;
    memcpy(&root->hw_info, info, sizeof(*info));
    root->numa.nums = info->numa_count;

    atexit(root_fini);
    return root;
}

int main(int argc, char *argv[])
{
    int i = 0;
    int ret = 0;
    int fd = -1;
    pthread_t tid = {0};
    struct root *root = NULL;
    struct hw_info info = {0};

    ret = daemon(0, 0);
    if (ret != 0) {
        LOG_ERROR("daemon failure: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    fd = single_instance(RUN_LOCK_FILE);
    signal_process();

    ret = dpdk_init(argc, argv, &info);
    if (ret < 0) {
        return EXIT_FAILURE;
    }

    argc -= ret;
    argv += ret;

    s_root = root_init(&info, fd, RUN_LOCK_FILE);
    ret = pthread_create(&tid, NULL, api_startup, &s_root->numa);
    if (ret != 0) {
        LOG_ERROR("startup api thread failure: %s", strerror(ret));
        return EXIT_FAILURE;
    }

    dpdk_thread_startup(dp_startup, s_root);
    return EXIT_SUCCESS;
}
