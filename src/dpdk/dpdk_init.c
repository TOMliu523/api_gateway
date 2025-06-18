/************************************************
 * filename: dpdk_init.c
 * function:
 * description:
 ***********************************************/

#include <errno.h>
#include <stdint.h>
#include <net/if.h>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_lcore.h>
#include <rte_errno.h>
#include <rte_memcpy.h>
#include <rte_cycles.h>
#include <rte_random.h>
#include <rte_thread.h>
#include <rte_ethdev.h>
#include <rte_memory.h>
#include <rte_ethdev.h>
#include <rte_version.h>

#include "log.h"
#include "type.h"
#include "macro.h"
#include "dpdk_type.h"
#include "dpdk_init.h"

#define DPDK_RX_DESC_DEFAULT 2048
#define DPDK_RX_TX_REDUNDANT 512
#define DPDK_1M (RTE_PGSIZE_2M / 2)

struct numa_cpu {
    int numa_count;
    struct numa_to_cpu {
        int numa_id;
        int hw_numa_id;
        int count;
        int hw_cpu_id[NUMA_CPU_MAX];
        int cpu_lcore[NUMA_CPU_MAX];
    } n2c[NUMA_MAX];

    int cpu_count;
    struct cpu_to_numa {
        int hw_cpu_id;
        int dpdk_cpu_id;
        int numa_cpu_id;
        int dpdk_numa_id;
        int hw_numa_id;
    } c2n[NUMA_MAX * NUMA_CPU_MAX];
};

uint8_t s_rss_key[] = {
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A,
};

static struct numa_cpu s_numa_cpu;
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

static void _dpdk_numa_to_cpu_init(struct numa_cpu *numa_cpu, struct hw_info *info)
{
    int i = 0;
    int count = 0;
    int lcore_id = 0;
    int hw_numa_id = 0;
    int numa[NUMA_MAX] = {0};

    numa_cpu->numa_count = info->numa_count;
    for (i = 0; i < numa_cpu->numa_count; i++) {
        struct numa_to_cpu *n2c = &numa_cpu->n2c[i];

        count = 0;
        n2c->numa_id = i;
        n2c->hw_numa_id = rte_socket_id_by_idx(i);
        RTE_LCORE_FOREACH(lcore_id) {
            hw_numa_id = rte_lcore_to_socket_id(lcore_id);

            RUNTIME_ASSERT(hw_numa_id < NUMA_MAX);

            if (hw_numa_id == n2c->hw_numa_id) {
                n2c->hw_cpu_id[count] = lcore_id;
                n2c->cpu_lcore[count] = rte_lcore_index(lcore_id);

                count += 1;
            }
        }

        n2c->count = count;
    }

    numa_cpu->cpu_count = info->cpu_count;
    RTE_LCORE_FOREACH(lcore_id) {
        int cpu_lcore = rte_lcore_index(lcore_id);
        struct cpu_to_numa *c2n = &numa_cpu->c2n[cpu_lcore];

        c2n->hw_cpu_id = lcore_id;
        c2n->dpdk_cpu_id = cpu_lcore;
        c2n->hw_numa_id = rte_lcore_to_socket_id(lcore_id);
        for (int j = 0; j < NUMA_MAX; j++) {
            if (c2n->hw_numa_id == rte_socket_id_by_idx(j)) {
                c2n->dpdk_numa_id = j;
                break;
            }
        }
    }

    for (i = 0; i < numa_cpu->cpu_count; i++) {
        struct cpu_to_numa *one = &numa_cpu->c2n[i];
        one->numa_cpu_id = numa[one->hw_numa_id]++;
    }
}

static int _dpdk_memory_info(const struct rte_memseg_list *msl, const struct rte_memseg *ms, void *arg)
{
    struct hw_info *info = arg;

    if (msl != NULL) {
        info->hugepage_size = msl->page_sz;
        info->total_memory += msl->len;
    }

    return 0;
}

static INLINE void _dpdk_info(struct hw_info *info)
{
    info->numa_count = rte_socket_count();
    info->cpu_count = rte_lcore_count();
    info->nic_count = rte_eth_dev_count_avail();
    rte_memseg_walk(_dpdk_memory_info, info);
}

static INLINE int _dpdk_up_align(int cur_nums, int align)
{
    cur_nums += DPDK_RX_TX_REDUNDANT;
    if (cur_nums % align == 0) {
        return cur_nums;
    }

    return (cur_nums / align + 1) * align;
}

static INLINE void _dpdk_mac_show(uint8_t mac[], const char *desc)
{
    LOG_INFO("%s: %02hx:%02hx:%02hx:%02hx:%02hx:%02hx", desc, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int _dpdk_init_nic(const struct numa_cpu *nc, const struct hw_info *info)
{
    int ret = 0;
    int port = 0;
    int retry = 0;
    uint32_t max_desc = 0;
    uint16_t max_queues = 0;
    struct rte_eth_rxconf rx = {0};
    struct rte_eth_txconf tx = {0};
    struct dpdk_eth_info dev = {0};
    struct dpdk_eth_conf conf = {0};
    struct rte_mempool *pool[NUMA_MAX] = {NULL};

    RTE_ETH_FOREACH_DEV(port) {
        ret = rte_eth_dev_info_get(port, &dev);
        if (ret != 0) {
            LOG_ERROR("port_id %d rte_eth_dev_info_get failure.", port);
            goto _quit;
        }

        if (dev.max_rx_queues < info->cpu_count || dev.max_tx_queues < info->cpu_count) {
            LOG_ERROR("port(%d) max rx queues(%d) or max tx queues(%d) less then cpu count(%d)",
                      port, dev.max_rx_queues, dev.max_tx_queues, info->cpu_count);
            goto _quit;
        }

        rte_memcpy(&conf, &s_eth_conf, sizeof(conf));

        LOG_INFO("Supported RSS hash functions: 0x%lx\n", dev.flow_type_rss_offloads);
        conf.rx_adv_conf.rss_conf.rss_hf &= dev.flow_type_rss_offloads;

        max_queues = info->cpu_count;
        max_desc = _dpdk_up_align(dev.tx_desc_lim.nb_max + dev.rx_desc_lim.nb_max, dev.rx_desc_lim.nb_align);

        LOG_INFO("max_rx_queues = %u, max_tx_queues = %u, rx nb_min = %d, rx nb_max = %d, rx nb_min = %d, tx nb_max = %d, "
                  "rx align = %d, tx align = %d, rx nb_seg_max = %d, tx nb_seg_max = %d",
                  dev.max_rx_queues,
                  dev.max_tx_queues,
                  dev.rx_desc_lim.nb_min,
                  dev.rx_desc_lim.nb_max,
                  dev.tx_desc_lim.nb_min,
                  dev.tx_desc_lim.nb_max,
                  dev.rx_desc_lim.nb_align,
                  dev.tx_desc_lim.nb_align,
                  dev.rx_desc_lim.nb_seg_max,
                  dev.tx_desc_lim.nb_seg_max);

        if ((dev.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) != 0) {
            // conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
        }

        ret = rte_eth_dev_configure(port, max_queues, max_queues, &conf);
        if (ret != 0) {
            LOG_ERROR("Failed to configure port %u: %s.", port, strerror(-ret));
            goto _quit;
        }

        if (pool[0] == NULL) {
            for (int i = 0; i < nc->numa_count; i++) {
                char name[64] = "";
                snprintf(name, sizeof(name), "NUMA_%d_POOL", i);
                LOG_INFO("MAX desc = %u.", max_desc * nc->n2c[i].count * info->nic_count);
                pool[i] = rte_pktmbuf_pool_create(name,
                                                  max_desc * nc->n2c[i].count * info->nic_count,
                                                  256,
                                                  128,
                                                  RTE_MBUF_DEFAULT_BUF_SIZE,
                                                  i);
                if (pool[i] == NULL) {
                    LOG_ERROR("Failure rte_pktmbuf_pool_create %s.", strerror(rte_errno));
                    goto _quit;
                }
            }
        }

        rx = dev.default_rxconf;
        rx.rx_drop_en = 1;
        rx.offloads = conf.rxmode.offloads;

        for (int i = 0; i < info->cpu_count; i++) {
            int hw_numa_id = nc->c2n[i].hw_numa_id;
            int desc = MIN(DPDK_RX_DESC_DEFAULT, dev.rx_desc_lim.nb_max);

            ret = rte_eth_rx_queue_setup(port,
                                         i,
                                         desc,
                                         hw_numa_id,
                                         &rx,
                                         pool[nc->c2n[i].dpdk_numa_id]);
            if (ret != 0) {
                LOG_ERROR("Failure port(%d) queue(%d) rte_eth_rx_queue_setup: %s.", port, i, strerror(-ret));
                goto _quit;
            }
        }

        tx = dev.default_txconf;
        tx.offloads = conf.txmode.offloads;

        for (int i = 0; i < info->cpu_count; i++) {
            int hw_numa_id = nc->c2n[i].hw_numa_id;
            int desc = MIN(DPDK_RX_DESC_DEFAULT, dev.rx_desc_lim.nb_max);

            ret = rte_eth_tx_queue_setup(port,
                                         i,
                                         desc,
                                         hw_numa_id,
                                         &tx);
            if (ret != 0) {
                LOG_ERROR("Failure port(%d) queue(%d) rte_eth_tx_queue_setup: %s", port, i, strerror(-ret));
                goto _quit;
            }
        }

        for (;;) {
            ret = rte_eth_dev_start(port);
            if (ret == 0) {
                break;
            } else if (ret == -EAGAIN && retry < 100) {
                retry += 1;
                continue;
            } else {
                LOG_ERROR("Failure port(%d) rte_eth_dev_start: %s.", port, strerror(-ret));
                goto _quit;
            }
        }

        ret = rte_eth_promiscuous_enable(port);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) rte_eth_promiscuous_enable: %s", port, strerror(-ret));
        }

        ret = rte_eth_allmulticast_enable(port);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) rte_eth_allmulticast_enable: %s", port, strerror(-ret));
        }

        struct rte_eth_link link = {0};
        ret = rte_eth_link_get(port, &link);
        if (ret != 0) {
            LOG_ERROR("Failure port(%d) rte_eth_link_get: %s", port, strerror(-ret));
        }

        LOG_INFO("link_speed = %u, link_duplex = %u, link_autoneg = %u, link_status = %d",
                  link.link_speed, link.link_duplex, link.link_autoneg, link.link_status);
    }

    uint16_t nums = 0;
    struct rte_mbuf *pkts[256] = {NULL};

    retry = 0;
    int flag = 0;
    for (;;) {
    RTE_ETH_FOREACH_DEV(port) {
        for (int i = 0; i < info->cpu_count; i++) {
            nums += rte_eth_rx_burst(port, i, pkts + nums, 256 - nums);
        }
    }

    for (int i = 0; i < nums; i++) {
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkts[i], struct rte_ether_hdr *);
        _dpdk_mac_show(eth->dst_addr.addr_bytes, "dst ether");
        _dpdk_mac_show(eth->src_addr.addr_bytes, "src ether");
        LOG_INFO("ether_type: %#X", htons(eth->ether_type));

        flag = 1;
    }

    if (flag) {
        retry += 1;
        if (retry == 10) {
            break;
        }
    }

    nums = 0;
    flag = 0;
    }

    return 0;

_quit:
    for (int i = 0; i < nc->numa_count; i++) {
        if (pool[i] == NULL) {
            break;
        }

        rte_mempool_free(pool[i]);
        pool[i] = NULL;
    }
    return -1;
}

int dpdk_init(int argc, char *argv[], void *output)
{
    int ret = 0;

    RUNTIME_ASSERT(argv != NULL);

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        LOG_ERROR("rte_eal_init failure. %s ", rte_strerror(rte_errno));
        return -1;
    }

    rte_srand(rte_rdtsc());

    _dpdk_info(output);
    _dpdk_numa_to_cpu_init(&s_numa_cpu, output);

    ret = _dpdk_init_nic(&s_numa_cpu, output);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

void dpdk_thread_startup(void *f, void *arg)
{
    RUNTIME_ASSERT(f != NULL);

    LOG_INFO("STARTUP DPDK THREAD.");
    rte_eal_mp_remote_launch(f, arg, CALL_MAIN);
}

void dpdk_thread_set_name(uint8_t numa_idx, uint16_t dpdk_cpu_id)
{
    char name[RTE_THREAD_NAME_SIZE + 1] = "";

    snprintf(name, sizeof(name), "DATAPLANE_%X_%03d", numa_idx, dpdk_cpu_id);
    rte_thread_set_name(rte_thread_self(), name);
}

void dpdk_thread_info(uint8_t *numa_idx, uint8_t *local_idx, uint16_t *cpu_lcore, uint16_t *hw_numa_id, uint16_t *hw_cpu_id)
{
    struct numa_cpu *nc = &s_numa_cpu;

    RUNTIME_ASSERT(numa_idx != NULL && local_idx != NULL && cpu_lcore != NULL && hw_numa_id != NULL && hw_cpu_id != NULL);

    *hw_cpu_id = rte_lcore_id();
    *hw_numa_id = rte_socket_id();
    *cpu_lcore = rte_lcore_index(*hw_cpu_id);
    *local_idx = nc->c2n[*cpu_lcore].numa_cpu_id;
    *numa_idx = nc->c2n[*cpu_lcore].dpdk_numa_id;
}