/************************************************
 * filename: log.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rte_log.h>

#include "log.h"
#include "macro.h"

#define LOG_FILENAME "/var/log/api_gateway.log"

static int s_log_fd;
static FILE *s_logfile;

static PROC_INIT void log_init(void)
{
    int fd = -1;

    for (;;) {
        if (access(LOG_FILENAME, F_OK) == 0) {
            fd = open(LOG_FILENAME, O_WRONLY | O_APPEND);
            if (fd >= 0) {
                break;
            } else if (fd < 0 && errno != ENOENT) {
                fprintf(stderr, "Open log file(%s) failure.", LOG_FILENAME);
                return;
            }
        } else {
            fd = open(LOG_FILENAME, O_CREAT | O_EXCL | O_WRONLY | O_APPEND, 0666);
            if (fd >= 0) {
                break;
            } else if (fd < 0 && errno != EEXIST) {
                fprintf(stderr, "create log file(%s) failure.", LOG_FILENAME);
                return;
            }
        }
    }

    s_log_fd = fd;

    s_logfile = fopen(LOG_FILENAME, "a+");
    if (s_logfile != NULL) {
        rte_openlog_stream(s_logfile);
        rte_log_set_level(RTE_LOGTYPE_EAL, LOG_LEVEL);
    }
}

static PROC_FINI void log_fini(void)
{
    close(s_log_fd);
    fclose(s_logfile);
}

void log_write(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vdprintf(s_log_fd, format, ap);
    va_end(ap);
}