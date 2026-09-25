/************************************************
 * filename: dpdk_port.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <net/if.h>
#include <stdint.h>

#include <rte_ethdev.h>

#include "log.h"
#include "macro.h"
#include "dpdk_type.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_inner.h"
#include "dpdk_common.h"

#define DPDK_SPEED_NUMS_MAX 32
#define DPDK_RX_DESC_DEFAULT 4096
#define DPDK_TX_DESC_DEFAULT 4096

uint8_t s_rss_key[] = {
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A,
};

struct dpdk_speed {
    int nums;
    struct dpdk_speed_ex {
        const char *prefix;
        int type_nums;
        int nums; // Users later need to perform reverse port lookup by name from static structures
    } ex[DPDK_SPEED_NUMS_MAX];
};

struct dpdk_port_st {
    struct dpdk_speed speed;
    struct port_name port_name;
    struct rte_eth_conf eth_conf;
};

static struct dpdk_port_st s_dpdk_port = {
    .eth_conf = {
        .link_speeds = RTE_ETH_LINK_SPEED_AUTONEG,
        .rxmode = {
            .mq_mode = RTE_ETH_MQ_RX_RSS,
            .offloads = RTE_ETH_RX_OFFLOAD_RSS_HASH,
        },
        .rx_adv_conf = {
            .rss_conf = {
                .rss_key = s_rss_key,
                .rss_key_len = ARR_NUMS(s_rss_key),
                .rss_hf = (RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP),
                .algorithm = RTE_ETH_HASH_FUNCTION_DEFAULT,
            },
        },
        .txmode = {
            .mq_mode = RTE_ETH_MQ_TX_NONE,
        },
    },
    .speed = {
        .nums = 16,
        .ex[0] = { .prefix = "NONE", },
        .ex[1] = { .prefix = "10ME", },
        .ex[2] = { .prefix = "100ME", },
        .ex[3] = { .prefix = "GE", },
        .ex[4] = { .prefix = "2.5GE", },
        .ex[5] = { .prefix = "5GE", },
        .ex[6] = { .prefix = "10GE", },
        .ex[7] = { .prefix = "20GE", },
        .ex[8] = { .prefix = "25GE", },
        .ex[9] = { .prefix = "40GE", },
        .ex[10] = { .prefix = "50GE", },
        .ex[11] = { .prefix = "56GE", },
        .ex[12] = { .prefix = "100GE", },
        .ex[13] = { .prefix = "200GE", },
        .ex[14] = { .prefix = "400GE", },
        .ex[15] = { .prefix = "UNKNOWN", },
    },
};

static int dpdk_port_name_init(void)
{
    int ret = 0;
    int port = 0;
    int nbytes = 0;
    int speed_nums = 0;
    struct rte_eth_link link = {0};
    struct port_name *pn = &s_dpdk_port.port_name;

    RTE_ETH_FOREACH_DEV(port) {
        struct dpdk_speed_ex *ex = NULL;
        struct port_name_entry *one = &pn->entrys[port];

        one->port = port;
        ret = rte_eth_dev_get_name_by_port(port, one->pci);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) rte_eth_dev_get_name_by_port: %s", port, strerror(-ret));
            return -1;
        }

        ret = rte_eth_link_get_nowait(port, &link);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) rte_eth_link_get_nowait: %s", port, strerror(-ret));
            return -1;
        }

        switch (link.link_speed) {
        case RTE_ETH_SPEED_NUM_NONE: speed_nums = 0; break;
        case RTE_ETH_SPEED_NUM_10M: speed_nums = 1; break;
        case RTE_ETH_SPEED_NUM_100M: speed_nums = 2; break;
        case RTE_ETH_SPEED_NUM_1G: speed_nums = 3; break;
        case RTE_ETH_SPEED_NUM_2_5G: speed_nums = 4; break;
        case RTE_ETH_SPEED_NUM_5G: speed_nums = 5; break;
        case RTE_ETH_SPEED_NUM_10G: speed_nums = 6; break;
        case RTE_ETH_SPEED_NUM_20G: speed_nums = 7; break;
        case RTE_ETH_SPEED_NUM_25G: speed_nums = 8; break;
        case RTE_ETH_SPEED_NUM_40G: speed_nums = 9; break;
        case RTE_ETH_SPEED_NUM_50G: speed_nums = 10; break;
        case RTE_ETH_SPEED_NUM_56G: speed_nums = 11; break;
        case RTE_ETH_SPEED_NUM_100G: speed_nums = 12; break;
        case RTE_ETH_SPEED_NUM_200G: speed_nums = 13; break;
        case RTE_ETH_SPEED_NUM_400G: speed_nums = 14; break;
        case RTE_ETH_SPEED_NUM_UNKNOWN: speed_nums = 15; break;
        }

        ex = &s_dpdk_port.speed.ex[speed_nums];
        ex->type_nums += 1;
        ex->nums = port;

        nbytes = snprintf(one->name, sizeof(one->name), "%s.%d", ex->prefix, ex->type_nums);
        one->name[nbytes] = 0;

        pn->count += 1;
    }

    return 0;
}

const struct port_name *dpdk_port_name_get(void)
{
    return &s_dpdk_port.port_name;
}

int dpdk_port_startup(int port)
{
    int ret = 0;
    int retry = 0;
    int max_queues = 0;
    int max_rx_desc = 0;
    int max_tx_desc = 0;
    void * const *pool = NULL;
    struct numa_cpu *nc = NULL;
    struct rte_eth_conf conf = {0};
    struct rte_eth_rxconf rx = {0};
    struct rte_eth_txconf tx = {0};
    struct rte_eth_dev_info dev = {0};

    nc = dpdk_numa_cpu_get();
    pool = dpdk_pool_pktmbuf_get();

    ret = rte_eth_dev_info_get(port, &dev);
    if (ret != 0) {
        LOG_ERROR("Failure port_id(%d) rte_eth_dev_info_get: %s.", port, strerror(-ret));
        return -1;
    }

    if (dev.max_rx_queues < nc->cpu_count || dev.max_tx_queues < nc->cpu_count) {
        LOG_ERROR("port(%d) max rx(%d) tx(%d) queues less then cpu count",
                   port, dev.max_rx_queues, dev.max_tx_queues);
        return -1;
    }

    rte_memcpy(&conf, &s_dpdk_port.eth_conf, sizeof(conf));
    conf.rx_adv_conf.rss_conf.rss_hf &= dev.flow_type_rss_offloads;
    if ((dev.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) != 0) {
        // conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    max_queues = nc->cpu_count;
    max_rx_desc = MIN(DPDK_RX_DESC_DEFAULT, dev.rx_desc_lim.nb_max);
    max_tx_desc = MIN(DPDK_TX_DESC_DEFAULT, dev.tx_desc_lim.nb_max);

    ret = rte_eth_dev_configure(port, max_queues, max_queues, &conf);
    if (ret != 0) {
        LOG_ERROR("Failed to configure port(%d): %s", port, strerror(-ret));
        return -1;
    }

    rx = dev.default_rxconf;
    rx.rx_drop_en = 1;
    rx.offloads = conf.rxmode.offloads;

    for (int i = 0; i < nc->cpu_count; i++) {
        int numa_id = nc->c2n[i].numa_id;
        int hw_numa_id = nc->c2n[i].hw_numa_id;

        ret = rte_eth_rx_queue_setup(port, i, max_rx_desc, hw_numa_id, &rx, pool[numa_id]);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) queue(%d) rte_eth_rx_queue_setup: %s", port, i, strerror(-ret));
            return -1;
        }
    }

    tx = dev.default_txconf;
    tx.offloads = conf.txmode.offloads;

    for (int i = 0; i < nc->cpu_count; i++) {
        int numa_id = nc->c2n[i].numa_id;
        int hw_numa_id = nc->c2n[i].hw_numa_id;

        ret = rte_eth_tx_queue_setup(port, i, max_tx_desc, hw_numa_id, &tx);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) queue(%d) rte_eth_tx_queue_setup: %s", port, i, strerror(-ret));
            return -1;
        }
    }

    for (;;) {
        ret = rte_eth_dev_start(port);
        if (ret == 0) {
            break;
        } else if (ret == -EAGAIN && retry < 3) { // 3 is an estimated number.
            retry += 1;
            continue;
        } else {
            LOG_ERROR("Failure port(%d) rte_eth_dev_start: %s", port, strerror(-ret));
            return -1;
        }
    }

    ret = rte_eth_promiscuous_enable(port);
    if (ret != 0) {
        LOG_ERROR("Failure port(%d) rte_eth_promiscuous_enable: %s", port, strerror(-ret));
        return -1;
    }

    ret = rte_eth_allmulticast_enable(port);
    if (ret != 0) {
        LOG_ERROR("Failure port(%d) rte_eth_allmulticast_enable: %s", port, strerror(-ret));
        return -1;
    }

    return 0;
}

uint16_t dpdk_port_by_name_get(const char *name)
{
    const char *tmp = NULL;
    const char *anchor = NULL;

    if (UNLIKELY(name == NULL)) {
        LOG_ERROR("Parameter exception.");
        return (uint16_t)-1;
    }

    LOG_INFO("port name: %s", name);
    anchor = strrchr(name, '.');
    if (anchor == NULL) {
        LOG_ERROR("Interface name format error: %s", name);
        return (uint16_t)-1;
    }

    anchor += 1;
    tmp = anchor;

    if (*tmp == 0) {
        LOG_ERROR("Interface name format error: %s", name);
        return -1;
    }

    while (*tmp != 0) {
        if ((!isdigit(*tmp))) {
            LOG_ERROR("Interface name format error: %s", name);
            return -1;
        }

        tmp += 1;
    }

    return atoi(anchor);
}

int dpdk_port_restart(int port)
{
    return rte_eth_dev_start(port);
}

int dpdk_port_stop(int port)
{
    return rte_eth_dev_stop(port);
}

int dpdk_port_init(void)
{
    int ret = 0;
    int port = 0;

    ret = dpdk_pool_pktmbuf_create();
    if (ret != 0) {
        return -1;
    }

    ret = dpdk_port_name_init();
    if (ret != 0) {
        return -1;
    }

    /*RTE_ETH_FOREACH_DEV(port) {
        ret = dpdk_port_startup(port);
        if (ret < 0) {
            return -1;
        }
    }*/

    return 0;
}