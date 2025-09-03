/*****************************************************************************
 * filename: ip4.c
 * function:
 * description:
 *****************************************************************************/

#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "l2.h"
#include "ip4.h"
#include "log.h"
#include "list.h"
#include "type.h"
#include "macro.h"
#include "errcode.h"
#include "dpdk_ip4.h"
#include "dpdk_fib.h"
#include "dpdk_rcu.h"
#include "dpdk_icmp.h"
#include "dpdk_core.h"
#include "dpdk_spinlock.h"

// Big Endian ip
#define IP4_BUCKET_IDX(x) ((x) >> 16)
#define IP4_BUCKET_MAX (1 << 16)
#define IP4_INFO_MAX IP4_BUCKET_MAX

struct ip4_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib *fib[DPDK_ETHPORT_MAX];
    /*
     * Each bucket contains multiple nodes, sorted in ascending order by IP address
     * (based on big-endian representation). If the IP addresses are equal,
     * the nodes are further sorted in ascending order by port number
     */
    struct list_head head[IP4_BUCKET_MAX];
    struct ip4_info *master[DPDK_ETHPORT_MAX];
    struct ip4_info store[IP4_INFO_MAX];
};

// Release the ipv4_manage structure.
static void _ip4_conf_manage_destroy(struct ip4_manage *manage)
{
    if (manage == NULL) {
        return;
    }

    for (int i = 0; i < manage->nic_count; i++) {
        if (manage->fib[i] != NULL) {
            dpdk_fib_destroy(manage->fib[i]);
        }
    }

    dpdk_free(manage);
}

static int _ip4_conf_manage_add(struct ip4_manage *manage, const struct ip4_info *one)
{
    int ret = 0;
    int nums = 0;
    uint32_t host_ip = 0;
    char ip_str[CACHE_LINE] = "";
    struct ip4_info *cur = NULL;
    struct list_head *prev = NULL;
    struct ip4_info *store = NULL;
    int idx = IP4_BUCKET_IDX(one->ip);
    struct list_head *head = &manage->head[idx];

    nums = manage->ip_count;
    store = &manage->store[nums];
    *store = *one;
    INIT_LIST_HEAD(&store->node);
    if (manage->master[one->port] == NULL && one->type == IP_MASTER) {
        manage->master[one->port] = store;
    }

    host_ip = dpdk_be_to_cpu_32(one->ip);
    ret = dpdk_fib_add(manage->fib[one->port], host_ip, one->mask, nums);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Failure dpdk_fib_add: %s", strerror(-ret));
        return ERRCODE_INNER;
    }

    prev = head;
    list_for_each_entry(cur, head, node) {
        if (cur->ip == one->ip) {
            if (cur->port == one->port) {
                inet_ntop(AF_INET, &one->ip, ip_str, sizeof(ip_str));
                LOG_ERROR("IP(%s) and port(%d) already exists.", ip_str, one->port);
                return ERRCODE_IP_EXIST;
            } else if (cur->port < one->port) {
                prev = &cur->node;
            } else {
                break;
            }
        } else if (cur->ip < one->ip) {
            prev = &cur->node;
        } else {
            break;
        }
    }

    list_add(&store->node, prev);
    manage->ip_count = nums + 1;

    return 0;
}

/*
 * It is not allowed to assign two IP addresses on the same network interface
 * if they belong to the same subnet or have overlapping subnets,
 * such as 192.168.10.0/24 and 192.168.10.0/28.
 */
static int _ip4_conf_manage_add_check(struct ip4_manage *manage, const struct ip4_info *info, int count)
{
    int ret = 0;
    uint8_t mask = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";
    const struct ip4_info *one = NULL;

    if (UNLIKELY(manage == NULL || manage->ip_count == 0) && count <= IP4_INFO_MAX) {
        return 0;
    }

    if (UNLIKELY(manage->ip_count + count > IP4_INFO_MAX)) {
        LOG_ERROR("Maximum supported IP address count(%d) exceeded", IP4_INFO_MAX);
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];

        ret = dpdk_fib_lookup(manage->fib[one->port], (uint32_t *)&one->ip, &next_hop, 1);
        if (UNLIKELY(ret != 0)) {
            LOG_ERROR("Inner error.");
            return ERRCODE_INNER;
        }

        if (UNLIKELY(ret == 0 && next_hop != DPDK_FIB_DEFAULT && manage->store[next_hop].mask == one->mask)) {
            mask = one->mask;
            inet_ntop(AF_INET, &one->ip, ip_str, sizeof(ip_str));
            LOG_ERROR("IP address conflict — another IP(%s/%d) in the same subnet is already configured.", ip_str, mask);
            return ERRCODE_SUBNET_EXIST;
        }
    }

    return 0;
}

/*
 * 1. Check that all IP addresses exist.
 * 2. Verify that the quantity meets expectations.
 */
static int _ip4_conf_manage_del_check(struct ip4_manage *manage, const struct ip4_info *info, int count)
{
    int idx = 0;
    int exist = 0;
    char ip_str[CACHE_LINE] = "";
    struct ip4_info *cur = NULL;
    struct list_head *head = NULL;
    const struct ip4_info *one = NULL;

    if (UNLIKELY(manage->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception: origin %d, delete %d", manage->ip_count, count);
        return ERRCODE_INNER;
    }

    for (int i = 0; i < count; i++) {
        exist = 0;
        one = &info[i];

        idx = IP4_BUCKET_IDX(one->ip);
        head = &manage->head[idx];

        list_for_each_entry(cur, head, node) {
            if (cur->ip == one->ip && cur->port == one->port) {
                exist = !0;
                break;
            } else {
                continue;
            }
        }

        if (!exist) {
            inet_ntop(AF_INET, &one->ip, ip_str, sizeof(ip_str));
            LOG_ERROR("IP: %s, port: %d not exist", ip_str, one->port);
            return ERRCODE_IP_NOT_EXIST;
        }
    }

    return 0;
}

// Initialize a fresh ipv4_manage object with default/empty values.
static int _ip4_conf_manage_create(void **dst, int nic_count, int hw_numa_id)
{
    struct ip4_manage *manage = NULL;

    manage = dpdk_malloc_numa(sizeof(*manage), hw_numa_id);
    if (UNLIKELY(manage == NULL)) {
        LOG_ERROR("HW NUMA(%d) OOM.", hw_numa_id);
        return ERRCODE_OOM;
    }

    memset(manage, 0, sizeof(*manage));

    manage->ip_count = 0;
    manage->nic_count = nic_count;

    for (int i = 0; i < nic_count; i++) {
        manage->fib[i] = dpdk_fib_create(hw_numa_id, IP4_INFO_MAX);
        if (UNLIKELY(manage->fib[i] == NULL)) {
            goto _quit;
        }
    }

    for (int i = 0; i < IP4_BUCKET_MAX; i++) {
        INIT_LIST_HEAD(&manage->head[i]);
    }

    *dst = manage;
    return 0;

_quit:
    _ip4_conf_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _ip4_conf_manage_append(struct ip4_manage *dst, const struct ip4_manage *src, const struct ip4_info *info, int count)
{
    int ret = 0;

    for (int i = 0; i < src->ip_count; i++) {
        ret = _ip4_conf_manage_add(dst, &src->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _ip4_conf_manage_add(dst, &info[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _ip4_conf_manage_delete(struct ip4_manage *dst, const struct ip4_manage *src, const struct ip4_info *info, int count)
{
    int ret = 0;
    bool need_delete = false;
    const struct ip4_info *one = NULL;
    const struct ip4_info *store = NULL;

    for (int i = 0; i < src->ip_count; i++) {
        need_delete = false;
        store = &src->store[i];

        for (int j = 0; j < count; j++) {
            one = &info[j];
            if (one->ip == store->ip && one->port == store->port) {
                need_delete = true;
                break;
            }
        }

        if (need_delete) {
            continue;
        }

        ret = _ip4_conf_manage_add(dst, store);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

bool ip4_conf_manage_ip_is_local(const void *arg, uint32_t ip, uint8_t port)
{
    const struct ip4_info *cur = NULL;
    const struct list_head *head = NULL;
    const struct ip4_manage *ip4_manage = (const struct ip4_manage *)arg;

    if (arg == NULL) {
        return false;
    }

    head = &ip4_manage->head[IP4_BUCKET_IDX(ip)];
    list_for_each_entry(cur, head, node) {
        if (cur->ip == ip && cur->port == port) {
            return true;
        } else {
            continue;
        }
    }

    return false;
}

int ip4_conf_manage_create_and_append(void **dst, void *src, const struct ip4_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    const struct ip4_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    // Check whether the newly added IP address and subnet already exist
    ret = _ip4_conf_manage_add_check(src, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Only create the ipv4_manage structure.
    ret = _ip4_conf_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Add both new and existing data together
    ret = _ip4_conf_manage_append(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _ip4_conf_manage_destroy(*dst);
        *dst = NULL;
        return ret;
    }

    return 0;
}

int ip4_conf_manage_create_and_delete(void **dst, void *src, const struct ip4_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ip4_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || info == NULL || one->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, info: %p, count: %d).", dst, src, info, one->ip_count);
        return ERRCODE_INNER;
    }

    ret = _ip4_conf_manage_del_check(src, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Only create the ipv4_manage structure.
    ret = _ip4_conf_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _ip4_conf_manage_delete(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _ip4_conf_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

static int _ip4_get_by_port(uint32_t *ip, uint32_t target_ip, int port)
{
    int ret = 0;
    uint64_t next_hop = 0;
    struct ip4_manage *manage = rcu_dereference(tlv_th_cfg->ip4_manage);
    struct dpdk_fib *fib = manage->fib[port];

    ret = dpdk_fib_lookup(fib, &target_ip, &next_hop, 1);
    if (UNLIKELY(ret != 0 || next_hop == DPDK_FIB_DEFAULT)) {
        return -1;
    }

    *ip = manage->store[(uint16_t)next_hop].ip;
    return 0;
}

static INLINE void _ip4_is_local_bulk(void *data[], bool result[], int count)
{
    uint16_t idx = 0;
    struct ip4_info *cur = NULL;
    struct list_head *head = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct ip4_manage *manage = rcu_dereference(tlv_th_cfg->ip4_manage);

    for (int i = 0; i < count; i++) {
        bool hit = false;

        mbuf = data[i];
        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);

        idx = IP4_BUCKET_IDX(ip4hdr->dst_addr);
        head = &manage->head[idx];

        list_for_each_entry(cur, head, node) {
            if (ip4hdr->dst_addr == cur->ip) {
                if (mbuf->port == cur->port) {
                    hit = true;
                    break;
                } else if (mbuf->port < cur->port) {
                    continue;
                } else {
                    break;
                }
            } else if (ip4hdr->dst_addr < cur->ip) {
                continue;
            } else {
                break;
            }
        }

        result[i] = hit;
    }
}

static INLINE void _ip4_icmp_echo_reply(struct dpdk_mbuf *mbuf, struct dpdk_icmp_hdr *icmp)
{
    struct dpdk_eth_hdr *eth = dpdk_pktmbuf_eth(mbuf);
    struct dpdk_ip4_hdr *ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);

    // mac
    SWAP(eth->src_addr, eth->dst_addr);

    // ip
    SWAP(ip4hdr->dst_addr, ip4hdr->src_addr);

    // icmp
    dpdk_icmp_echo_reply_cksum(icmp);
}

static INLINE void _ip4_icmp_fragment(struct dpdk_mbuf *mbuf, uint16_t mtu)
{
    int rc = 0;
    int len = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_mbuf *pkt = NULL;
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct dpdk_eth_hdr *src_eth = dpdk_pktmbuf_eth(mbuf);
    struct dpdk_mac src_addr = src_eth->src_addr;
    struct dpdk_mac dst_addr = src_eth->dst_addr;

    dpdk_pktmbuf_adj(mbuf, sizeof(struct dpdk_eth_hdr));

    tx = &tlv_tx[mbuf->port];
    rc = dpdk_ip4_mbuf_fragment(mbuf, &tx->data[tx->count], 64, mtu, tlv_dp->pktmbuf_pool, tlv_dp->indirect_pool);
    if (UNLIKELY(rc < 0)) {
        return;
    }

    len = tx->count + rc;
    for (int i = tx->count; i < len; i++) {
        pkt = tx->data[i];

        pkt->port = mbuf->port;

        pkt->l2_len = sizeof(struct dpdk_eth_hdr);
        pkt->l3_len = sizeof(struct dpdk_ip4_hdr);

        eth = dpdk_pktmbuf_prepend(pkt, sizeof(struct dpdk_eth_hdr));
        eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_TYPE_IPV4);
        eth->dst_addr = src_addr;
        eth->src_addr = dst_addr;

        ip4hdr = dpdk_pktmbuf_ip4_hdr(pkt);
        ip4hdr->hdr_checksum = 0;
        pkt->ol_flags |= DPDK_TX_IP_TX_IP_CKSUM;
    }

    tx->count = len;
}

static void _ip4_icmp_process(struct dpdk_mbuf *data[], int count)
{
    uint16_t mtu = 0;
    uint16_t icmp_len = 0;
    uint8_t header_len = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_icmp_hdr *icmp = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;

    mtu = tlv_dp->mtu;
    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);
        header_len = dpdk_ip4_header_len(ip4hdr);
        icmp = dpdk_pktmbuf_icmp(mbuf, sizeof(struct dpdk_eth_hdr) + header_len);
        icmp_len = dpdk_be_to_cpu_16(ip4hdr->total_length) - header_len;

        if (LIKELY(dpdk_icmp_cksum_verify(mbuf, icmp, icmp_len))) {
            if (icmp->icmp_type == DPDK_ICMP_TYPE_ECHO_REQUEST && icmp->icmp_code == DPDK_ICMP_CODE_ECHO_REQUEST) {
                _ip4_icmp_echo_reply(mbuf, icmp);

                if (mbuf->pkt_len <= mtu) {
                    tx = &tlv_tx[mbuf->port];
                    tx->data[tx->count++] = mbuf;
                } else {
                    _ip4_icmp_fragment(mbuf, mtu);
                    tlv_drop->data[tlv_drop->count++] = mbuf;
                }
            }
        } else {
            tlv_drop->data[tlv_drop->count++] = mbuf;
        }
    }
}

void ip4_process(void *data[], int count)
{
    bool *result = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;

    result = (bool *)tlv_cache->data;
    _ip4_is_local_bulk(data, result, count);

    for (int i = 0; i < count; i++) {
        mbuf = data[i];

        if (UNLIKELY(!result[i])) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);
        if (UNLIKELY(!dpdk_ip4_header_cksum_verify(mbuf, ip4hdr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY(ip4hdr->version != 4 || ip4hdr->time_to_live == 0)) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (dpdk_ip4_mbuf_is_fragmented(ip4hdr)) {
            mbuf->l2_len = sizeof(struct dpdk_eth_hdr);
            mbuf->l3_len = dpdk_ip4_header_len(ip4hdr);
            mbuf = dpdk_ip4_mbuf_reassemble(tlv_dp->frag_handle, mbuf, tlv_dp->timer_cycles, ip4hdr);

            if (mbuf != NULL) {
                dpdk_ip_reassemble_finish(tlv_dp->frag_handle, mbuf->nb_segs);
                ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);
            } else {
                dpdk_ip_reassemble_pending(tlv_dp->frag_handle, tlv_dp->timer_cycles);
                continue;
            }
        }

        switch (ip4hdr->next_proto_id) {
        case IPPROTO_ICMP:
            tlv_icmp->data[tlv_icmp->count++] = mbuf;
            break;
        case IPPROTO_TCP:
        case IPPROTO_UDP:
        default:
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        }
    }

    if (tlv_icmp->count != 0) {
        _ip4_icmp_process((struct dpdk_mbuf **)tlv_icmp->data, tlv_icmp->count);
        tlv_icmp->count = 0;
    }
}

void ip4_arp_refresh(void)
{
    l2_arp_refresh(_ip4_get_by_port);
}

enum IP_LOCAL_CLASS ip4_local_class(uint16_t port, uint32_t ip)
{
    int ret = 0;
    uint16_t idx = 0;
    uint64_t next_hop = 0;
    struct dpdk_fib *fib = NULL;
    struct ip4_info *cur = NULL;
    struct list_head *head = NULL;
    struct ip4_manage *manage = rcu_dereference(tlv_th_cfg->ip4_manage);

    idx = IP4_BUCKET_IDX(ip);
    head = &manage->head[idx];

    list_for_each_entry(cur, head, node) {
        if (cur->ip == ip) {
            if (cur->port == port) {
                return IP_LOCAL_CLASS_SELF;
            } else if (cur->port < port) {
                continue;
            } else {
                break;
            }
        } else if (cur->ip < ip) {
            continue;
        } else {
            break;
        }
    }

    fib = manage->fib[port];
    ret = dpdk_fib_lookup(fib, &ip, &next_hop, 1);
    if (ret != 0 || next_hop == DPDK_FIB_DEFAULT) {
        return IP_LOCAL_CLASS_EXTERNAL;
    }

    return IP_LOCAL_CLASS_LAN;
}

void *ip4_startup(int nic_count, int hw_numa_id)
{
    int ret = 0;
    void *dst = NULL;

    ret = _ip4_conf_manage_create(&dst, nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return NULL;
    }

    return dst;
}

void ip4_manage_destroy(void *ptr)
{
    _ip4_conf_manage_destroy(ptr);
}