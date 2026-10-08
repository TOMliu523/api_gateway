/*****************************************************************************
 * filename: top.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __TOP_H__
#define __TOP_H__

#include <stdint.h>

#include "macro.h"
#include "atomic.h"
#include "dpdk_type.h"

struct hw_socket_stat {
    size_t heap_totalsz_bytes;
    unsigned free_count;
    unsigned alloc_count;
    size_t heap_freesz_bytes;
    size_t greatest_free_size;
    size_t heap_allocsz_bytes;
};

struct hw_info {
    int numa_count;
    int cpu_count;
    int nic_count;
    struct hw_socket_stat stat[NUMA_MAX];
};

struct boot_config {
    void *session;

    struct {
        char address[INET6_ADDRSTRLEN];
        int http_port;
        int https_port;
    };
};

struct data_root {
    bool inited;
    uint64_t startup_time;
    void *dataplane[CPU_MAX];
};

struct context {
    struct boot_config config;
    struct hw_info info;
    struct data_root root;
};

#endif // __TOP_H__