/************************************************
 * filename: dpdk_port.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <net/if.h>
#include <stdint.h>

#include <rte_ethdev.h>

#include "log.h"
#include "macro.h"
#include "dpdk_type.h"
#include "dpdk_inner.h"
#include "dpdk_common.h"

#define DPDK_RX_DESC_DEFAULT 4096
#define DPDK_TX_DESC_DEFAULT 4096
#define DPDK_PKTMBUF_CACHE_SIZE 256
#define DPDK_MAX_DESCRIPTORS_PER_CPU 50000

uint8_t s_rss_key[] = {
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A,
};

static struct dpdk_eth_conf s_eth_conf = {
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
};

static void *s_pktmbuf_pool[NUMA_MAX];

static void __dpdk_pktmbuf_fini(void *tmp[], int numa_count)
{
    for (int i = 0; i < numa_count; i++) {
        if (tmp[i] == NULL) {
            break;
        }

        rte_mempool_free(tmp[i]);
        tmp[i] = NULL;
    }
}

static void _dpdk_pktmbuf_fini(void)
{
    __dpdk_pktmbuf_fini(s_pktmbuf_pool, dpdk_numa_count_get());
}

static int _dpdk_pktmbuf_init()
{
    char name[CACHE_LINE] = "";
    void *tmp[NUMA_MAX] = {NULL};
    struct numa_cpu *nc = NULL;
    struct numa_to_cpu *n2c = NULL;

    nc = dpdk_numa_cpu_get();
    n2c = nc->n2c;

    for (int i = 0; i < nc->numa_count; i++) {
        snprintf(name, sizeof(name), "PKTMBUF_POOL_NUMA_%02d", i);
        tmp[i] = rte_pktmbuf_pool_create(name,
                                         n2c[i].count * DPDK_MAX_DESCRIPTORS_PER_CPU,
                                         DPDK_PKTMBUF_CACHE_SIZE,
                                         128,
                                         RTE_MBUF_DEFAULT_BUF_SIZE,
                                         n2c[i].hw_numa_id);
        if (tmp == NULL) {
            LOG_ERROR("Failure NUMA(%d) rte_pktmbuf_pool_create: %s", i, strerror(rte_errno));
            goto _quit;
        }

        s_pktmbuf_pool[i] = tmp[i];
    }

    atexit(_dpdk_pktmbuf_fini);
    return 0;

_quit:
    _dpdk_pktmbuf_fini();
    return -1;
}

int dpdk_port_startup(int port)
{
    int ret = 0;
    int retry = 0;
    int max_queues = 0;
    int max_rx_desc = 0;
    int max_tx_desc = 0;
    struct numa_cpu *nc = NULL;
    struct dpdk_eth_info dev = {0};
    struct dpdk_eth_conf conf = {0};
    struct rte_eth_rxconf rx = {0};
    struct rte_eth_txconf tx = {0};

    nc = dpdk_numa_cpu_get();
    ret = rte_eth_dev_info_get(port, &dev);
    if (ret != 0) {
        LOG_ERROR("port_id(%d) rte_eth_dev_info_get failure.", port);
        return -1;
    }

    if (dev.max_rx_queues < nc->cpu_count || dev.max_tx_queues < nc->cpu_count) {
        LOG_ERROR("port(%d) max rx(%d) tx(%d) queues less then cpu count",
                   port, dev.max_rx_queues, dev.max_tx_queues);
        return -1;
    }

    rte_memcpy(&conf, &s_eth_conf, sizeof(conf));
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
        int numa_id = nc->c2n[i].dpdk_numa_id;
        int hw_numa_id = nc->c2n[i].hw_numa_id;

        ret = rte_eth_rx_queue_setup(port, i, max_rx_desc, hw_numa_id, &rx, s_pktmbuf_pool[numa_id]);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) queue(%d) rte_eth_rx_queue_setup: %s", port, i, strerror(-ret));
            return -1;
        }
    }

    tx = dev.default_txconf;
    tx.offloads = conf.txmode.offloads;

    for (int i = 0; i < nc->cpu_count; i++) {
        int numa_id = nc->c2n[i].dpdk_numa_id;
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

int dpdk_port_init(void)
{
    int ret = 0;
    int port = 0;

    ret = _dpdk_pktmbuf_init();
    if (ret != 0) {
        return -1;
    }

    RTE_ETH_FOREACH_DEV(port) {
        ret = dpdk_port_startup(port);
        if (ret < 0) {
            return -1;
        }
    }

    return 0;
}