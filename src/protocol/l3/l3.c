/*****************************************************************************
 * filename: l3.c
 * function:
 * description:
 *****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "list.h"
#include "type.h"
#include "macro.h"
#include "dpdk_ip.h"
#include "errcode.h"
#include "protocol.h"
#include "dpdk_rcu.h"
#include "dpdk_fib.h"
#include "dpdk_hash.h"
#include "dpdk_fib6.h"
#include "dpdk_core.h"
#include "dpdk_icmp.h"
#include "dpdk_common.h"
#include "dpdk_spinlock.h"

// Big Endian ip
#define L3_IPv4_BUCKET_IDX(x) ((x) >> 16)
#define L3_IPv4_BUCKET_MAX (1 << 16)

#define L3_IPv6_BUCKET_MAX (1 << 16)

struct ipv4_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib *fib[DPDK_ETHPORT_MAX];
    /*
     * Each bucket contains multiple nodes, sorted in ascending order by IP address
     * (based on big-endian representation). If the IP addresses are equal,
     * the nodes are further sorted in ascending order by port number
     */
    struct list_head head[L3_IPv4_BUCKET_MAX];
    struct ipv4_info *master[DPDK_ETHPORT_MAX];
    struct ipv4_info store[L3_IPv4_BUCKET_MAX];
};

struct ipv6_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib6 *fib[DPDK_ETHPORT_MAX];
    struct dpdk_hash *hash[DPDK_ETHPORT_MAX];
    struct ipv6_info *master[DPDK_ETHPORT_MAX];
    struct ipv6_info store[L3_IPv4_BUCKET_MAX];
};

static dpdk_spinlock_t s_spinlock[NUMA_MAX] = {0};

// Release the ipv4_manage structure.
static void _l3_conf_ipv4_manage_destroy(struct ipv4_manage *manage)
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

static void _l3_conf_ipv6_manage_destroy(struct ipv6_manage *manage)
{
    if (manage == NULL) {
        return;
    }

    for (int i = 0; i < manage->nic_count; i++) {
        dpdk_hash_destroy(manage->hash[i]);
        dpdk_fib6_destroy(manage->fib[i]);
    }

    dpdk_free(manage);
}

static int _l3_conf_ipv6_info_cmp(const void *key1, const void *key2, size_t len)
{
    const __uint128_t ip1 = *(const __uint128_t *)&key1;
    const __uint128_t ip2 = *(const __uint128_t *)&key2;

    if (ip1 == ip2) {
        return 0;
    } else if (ip1 < ip2) {
        return -1;
    } else {
        return 1;
    }
}

static int _l3_conf_ipv4_manage_add(struct ipv4_manage *manage, const struct ipv4_info *one)
{
    int ret = 0;
    int nums = 0;
    uint32_t host_ip = 0;
    char ip_str[CACHE_LINE] = "";
    struct ipv4_info *cur = NULL;
    struct list_head *prev = NULL;
    struct ipv4_info *store = NULL;
    int idx = L3_IPv4_BUCKET_IDX(one->ip);
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

static int _l3_conf_ipv6_manage_add(struct ipv6_manage *manage, const struct ipv6_info *one)
{
    int ret = 0;
    int nums = 0;
    struct ipv6_info *store = NULL;

    nums = manage->ip_count;
    store = &manage->store[nums];
    *store = *one;

    if (manage->master[one->port] == NULL && store->type == IP_MASTER) {
        manage->master[one->port] = store;
    }

    ret = dpdk_fib6_add(manage->fib[one->port], &store->ipv6, store->mask, nums);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Failure dpdk_fib6_add: %s", strerror(-ret));
        return ERRCODE_INNER;
    }

    ret = dpdk_hash_add_kv(manage->hash[one->port], &one->ipv6, store);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Failure dpdk_hash_add_kv: %s", strerror(-rte_errno));
        return ERRCODE_INNER;
    }

    return 0;
}

/*
 * It is not allowed to assign two IP addresses on the same network interface
 * if they belong to the same subnet or have overlapping subnets,
 * such as 192.168.10.0/24 and 192.168.10.0/28.
 */
static int _l3_conf_ipv4_manage_add_check(struct ipv4_manage *manage, const struct ipv4_info *info, int count)
{
    int ret = 0;
    uint8_t mask = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";
    const struct ipv4_info *one = NULL;

    if (UNLIKELY(manage == NULL || manage->ip_count == 0)) {
        return 0;
    }

    if (UNLIKELY(manage->ip_count + count > L3_IPv4_BUCKET_MAX)) {
        LOG_ERROR("Maximum supported IP address count(%d) exceeded", L3_IPv4_BUCKET_MAX);
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];

        ret = dpdk_fib_lookup(manage->fib[one->port], (uint32_t *)&info->ip, &next_hop, 1);
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

static int _l3_conf_ipv6_manage_add_check(struct ipv6_manage *manage, const struct ipv6_info *info, int count)
{
    int ret = 0;
    uint8_t mask = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";
    const struct ipv6_info *one = NULL;

    if (UNLIKELY(manage == NULL || manage->ip_count == 0)) {
        return 0;
    }

    if (UNLIKELY(manage->ip_count + count > L3_IPv6_BUCKET_MAX)) {
        LOG_ERROR("Maximum supported IP address count(%d) exceeded", L3_IPv6_BUCKET_MAX);
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];

        ret = dpdk_fib6_lookup(manage->fib[one->port], &info->ipv6, &next_hop, 1);
        if (UNLIKELY(ret != 0)) {
            LOG_ERROR("Inner error.");
            return ERRCODE_INNER;
        }

        if (UNLIKELY(ret == 0 && next_hop != DPDK_FIB6_DEFAULT && manage->store[next_hop].mask == one->mask)) {
            mask = one->mask;
            inet_ntop(AF_INET6, &one->ipv6, ip_str, sizeof(ip_str));
            LOG_ERROR("IP address conflict - another IP(%s/%d) in the same subnet is already configured.", ip_str, mask);
            return ERRCODE_SUBNET_EXIST;
        }
    }

    return 0;
}

/*
 * 1. Check that all IP addresses exist.
 * 2. Verify that the quantity meets expectations.
 */
static int _l3_conf_ipv4_manage_del_check(struct ipv4_manage *manage, const struct ipv4_info *info, int count)
{
    int idx = 0;
    int exist = 0;
    char ip_str[CACHE_LINE] = "";
    struct ipv4_info *cur = NULL;
    struct list_head *head = NULL;
    const struct ipv4_info *one = NULL;

    if (UNLIKELY(manage->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception: origin %d, delete %d", manage->ip_count, count);
        return ERRCODE_INNER;
    }

    for (int i = 0; i < count; i++) {
        exist = 0;
        one = &info[i];

        idx = L3_IPv4_BUCKET_IDX(one->ip);
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

static int _l3_conf_ipv6_manage_del_check(struct ipv6_manage *manage, const struct ipv6_info *info, int count)
{
    int ret = 0;
    bool hit = false;
    char ip_str[CACHE_LINE] = "";
    struct ipv6_info *data = NULL;

    if (UNLIKELY(manage->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception: origin %d, delete %d", manage->ip_count, count);
        return ERRCODE_INNER;
    }

    // ret = dpdk_hash_lookup_data(manage->hash[info->port], (const void *)&info->ipv6, (void **)&data);
    if (UNLIKELY(ret != 0)) {
        inet_ntop(AF_INET6, &info->ipv6, ip_str, sizeof(ip_str));
        LOG_ERROR("IP: %s, port: %d not exists", ip_str, info->port);
        return ERRCODE_IP_NOT_EXIST;
    }

    return 0;
}

// Initialize a fresh ipv4_manage object with default/empty values.
static int _l3_conf_ipv4_manage_create(void **dst, int nic_count, int hw_numa_id)
{
    char name[CACHE_LINE] = "";
    struct ipv4_manage *manage = NULL;

    // Avoid name conflicts when creating FIB objects
    static uint32_t s_fib_version[DPDK_ETHPORT_MAX] = {0};

    manage = dpdk_malloc_numa(sizeof(*manage), hw_numa_id);
    if (UNLIKELY(manage == NULL)) {
        LOG_ERROR("HW NUMA(%d) OOM.", hw_numa_id);
        return ERRCODE_OOM;
    }

    memset(manage, 0, sizeof(*manage));

    manage->ip_count = 0;
    manage->nic_count = nic_count;

    for (int i = 0; i < nic_count; i++) {
        snprintf(name, sizeof(name), "IPv4_MANAGE_%u_%u_%u", hw_numa_id, i, s_fib_version[hw_numa_id]++);
        manage->fib[i] = dpdk_fib_create(name, hw_numa_id, L3_IPv4_BUCKET_MAX);
        if (UNLIKELY(manage->fib[i] == NULL)) {
            goto _quit;
        }
    }

    for (int i = 0; i < L3_IPv4_BUCKET_MAX; i++) {
        INIT_LIST_HEAD(&manage->head[i]);
    }

    *dst = manage;
    return 0;

_quit:
    _l3_conf_ipv4_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _l3_conf_ipv6_manage_create(void **dst, int nic_count, int hw_numa_id)
{
    struct ipv6_manage *manage = NULL;

    manage = dpdk_malloc_numa(sizeof(*manage), hw_numa_id);
    if (UNLIKELY(manage == NULL)) {
        LOG_ERROR("HA NUMA(%d) OOM.", hw_numa_id);
        return ERRCODE_OOM;
    }

    memset(manage, 0, sizeof(*manage));

    manage->ip_count = 0;
    manage->nic_count = nic_count;

    for (int i = 0; i < nic_count; i++) {
        manage->hash[i] = dpdk_hash_create(L3_IPv6_BUCKET_MAX, sizeof(union dpdk_ipv6_addr), hw_numa_id, _l3_conf_ipv6_info_cmp);
        if (UNLIKELY(manage->hash[i] == NULL)) {
            goto _quit;
        }
    }

    for (int i = 0; i < nic_count; i++) {
        manage->fib[i] = dpdk_fib6_create(hw_numa_id, L3_IPv6_BUCKET_MAX);
        if (UNLIKELY(manage->fib[i] == NULL)) {
            goto _quit;
        }
    }

    *dst = manage;
    return 0;

_quit:
    _l3_conf_ipv6_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _l3_conf_ipv4_manage_append(struct ipv4_manage *dst, const struct ipv4_manage *src, const struct ipv4_info *info, int count)
{
    int ret = 0;

    for (int i = 0; i < src->ip_count; i++) {
        ret = _l3_conf_ipv4_manage_add(dst, &src->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _l3_conf_ipv4_manage_add(dst, &info[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _l3_conf_ipv6_manage_append(struct ipv6_manage *dst, const struct ipv6_manage *src, const struct ipv6_info *info, int count)
{
    int ret = 0;
    int nums = 0;

    for (int i = 0; i < src->ip_count; i++) {
        ret = _l3_conf_ipv6_manage_add(dst, &src->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _l3_conf_ipv6_manage_add(dst, &info[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _l3_conf_ipv4_manage_delete(struct ipv4_manage *dst, const struct ipv4_manage *src, const struct ipv4_info *info, int count)
{
    int ret = 0;
    int nums = 0;
    bool need_delete = false;
    const struct ipv4_info *one = NULL;
    const struct ipv4_info *store = NULL;

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

        ret = _l3_conf_ipv4_manage_add(dst, store);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _l3_conf_ipv6_manage_delete(struct ipv6_manage *dst, const struct ipv6_manage *src, const struct ipv6_info *info, int count)
{
    int ret = 0;
    int nums = 0;
    bool need_delete = false;
    const struct ipv6_info *one = NULL;
    const struct ipv6_info *store = NULL;

    for (int i = 0; i < src->ip_count; i++) {
        need_delete = false;
        store = &src->store[i];

        for (int j = 0; j < count; j++) {
            one = &info[j];
            if (one->ipv6.big_addr == store->ipv6.big_addr && one->port == store->port) {
                need_delete = true;
                break;
            }
        }

        if (need_delete) {
            continue;
        }

        ret = _l3_conf_ipv6_manage_add(dst, store);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

bool l3_conf_ipv4_manage_ip_is_local(const void *arg, uint32_t ip, uint8_t port)
{
    const struct ipv4_info *cur = NULL;
    const struct list_head *head = NULL;
    const struct ipv4_manage *ipv4_manage = (const struct ipv4_manage *)arg;

    if (arg == NULL) {
        return false;
    }

    head = &ipv4_manage->head[L3_IPv4_BUCKET_IDX(ip)];
    list_for_each_entry(cur, head, node) {
        if (cur->ip == ip && cur->port == port) {
            return true;
        } else {
            continue;
        }
    }

    return false;
}

bool l3_conf_ipv6_manage_ip_is_local(const void *arg, const union dpdk_ipv6_addr *addr, uint8_t port)
{
    int ret = 0;
    const struct ipv6_info *cur = NULL;
    const struct ipv6_manage *ipv6_manage = (const struct ipv6_manage *)arg;

    if (arg == NULL) {
        return false;
    }

    // ret = dpdk_hash_lookup_bulk_data();

    return true;
}

int l3_conf_ipv4_manage_create_and_append(void **dst, void *src, const struct ipv4_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    const struct ipv4_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    // Check whether the newly added IP address and subnet already exist
    ret = _l3_conf_ipv4_manage_add_check(src, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    dpdk_spinlock_lock(&s_spinlock[hw_numa_id]);
    // Only create the ipv4_manage structure.
    ret = _l3_conf_ipv4_manage_create(dst, one->nic_count, hw_numa_id);
    dpdk_spinlock_unlock(&s_spinlock[hw_numa_id]);

    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Add both new and existing data together
    ret = _l3_conf_ipv4_manage_append(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ipv4_manage_destroy(*dst);
        *dst = NULL;
        return ret;
    }

    return 0;
}

int l3_conf_ipv4_manage_create_and_delete(void **dst, void *src, const struct ipv4_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ipv4_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || info == NULL || one->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, info: %p, count: %d).", dst, src, info, one->ip_count);
        return ERRCODE_INNER;
    }

    ret = _l3_conf_ipv4_manage_del_check(src, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Only create the ipv4_manage structure.
    ret = _l3_conf_ipv4_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ipv4_manage_delete(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ipv4_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

int l3_conf_ipv6_manage_create_and_append(void **dst, void *src, const struct ipv6_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ipv6_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    ret = _l3_conf_ipv6_manage_add_check(one, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ipv6_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ipv6_manage_append(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ipv6_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

int l3_conf_ipv6_manage_create_and_delete(void **dst, void *src, const struct ipv6_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ipv6_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    ret = _l3_conf_ipv6_manage_del_check(one, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ipv6_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ipv6_manage_destroy(*dst);
        return ret;
    }

    ret = _l3_conf_ipv6_manage_delete(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ipv6_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

static INLINE uint64_t _l3_ip_cksum_flag(int port)
{
    return tls_tx_offload[port] & DPDK_TX_IP_TX_IP_CKSUM;
}

static int _l3_ip_get_by_port(uint32_t *ip, uint32_t target_ip, int port)
{
    int ret = 0;
    uint64_t next_hop = 0;
    struct ipv4_manage *manage = rcu_dereference(tls_th_cfg->ipv4_manage);
    struct dpdk_fib *fib = manage->fib[port];

    ret = dpdk_fib_lookup(fib, &target_ip, &next_hop, 1);
    if (UNLIKELY(ret != 0 || next_hop == DPDK_FIB_DEFAULT)) {
        return -1;
    }

    *ip = manage->store[(uint16_t)next_hop].ip;
    return 0;
}

static INLINE void _l3_ipv4_is_local_bulk(void *mbufs[], bool result[], int count)
{
    int nums = 0;
    uint16_t idx = 0;
    bool hit = false;
    struct ipv4_info *cur = NULL;
    struct list_head *head = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ipv4 *ipv4 = NULL;
    struct ipv4_manage *manage = rcu_dereference(tls_th_cfg->ipv4_manage);

    for (int i = 0; i < count; i++) {
        mbuf = mbufs[i];
        ipv4 = dpdk_pktmbuf_ipv4(mbuf);

        idx = L3_IPv4_BUCKET_IDX(ipv4->dst_addr);
        head = &manage->head[idx];

        list_for_each_entry(cur, head, node) {
            if (ipv4->dst_addr == cur->ip) {
                if (mbuf->port == cur->port) {
                    hit = true;
                    result[nums++] = true;
                    break;
                } else if (mbuf->port < cur->port) {
                    continue;
                } else {
                    break;
                }
            } else if (ipv4->dst_addr < cur->ip) {
                continue;
            } else {
                break;
            }
        }

        if (!hit) {
            result[nums++] = false;
        } else {
            hit = false;
        }
    }
}

static INLINE void _l3_icmp_echo_reply(struct dpdk_mbuf *mbuf, struct dpdk_icmp *icmp)
{
    uint32_t tmp = 0;
    struct dpdk_eth *eth = dpdk_pktmbuf_eth(mbuf);
    struct dpdk_ipv4 *ipv4 = dpdk_pktmbuf_ipv4(mbuf);

    // mac
    SWAP(eth->src_addr, eth->dst_addr);

    // ip
    SWAP(ipv4->dst_addr, ipv4->src_addr);

    // icmp
    dpdk_icmp_echo_reply_cksum(icmp);
}

static INLINE void _l3_icmp_fragment(struct dpdk_mbuf *mbuf)
{
    int rc = 0;
    int len = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_eth *eth = NULL;
    struct dpdk_mbuf *pkt = NULL;
    struct dpdk_ipv4 *ipv4 = NULL;
    struct dpdk_eth *src_eth = NULL;

    src_eth = dpdk_pktmbuf_eth(mbuf);

    dpdk_pktmbuf_adj(mbuf, sizeof(struct dpdk_eth));

    tx = &tls_tx[mbuf->port];
    rc = dpdk_ipv4_mbuf_fragment(mbuf, &tx->data[tx->count], 64, tls_dp->mtu, tls_dp->pktmbuf_pool, tls_dp->indirect_pool);
    if (UNLIKELY(rc < 0)) {
        return;
    }

    len = tx->count + rc;
    for (int i = tx->count; i < len; i++) {
        pkt = tx->data[i];

        pkt->port = mbuf->port;

        pkt->l2_len = sizeof(struct dpdk_eth);
        pkt->l3_len = sizeof(struct dpdk_ipv4);

        eth = dpdk_pktmbuf_prepend(pkt, sizeof(struct dpdk_eth));
        eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_TYPE_IPV4);
        eth->dst_addr = src_eth->dst_addr;
        eth->src_addr = src_eth->src_addr;

        ipv4 = dpdk_pktmbuf_ipv4(pkt);
        ipv4->hdr_checksum = 0;
        pkt->ol_flags |= DPDK_TX_IP_TX_IP_CKSUM;
    }

    tx->count = len;
}

static INLINE void _l3_icmp_process(struct dpdk_mbuf *data[], int count)
{
    int nums = 0;
    uint32_t mask = 0;
    uint16_t icmp_len = 0;
    uint8_t header_len = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_icmp *icmp = NULL;
    struct dpdk_ipv4 *ipv4 = NULL;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ipv4 = dpdk_pktmbuf_ipv4(mbuf);
        header_len = dpdk_ipv4_header_len(ipv4);
        icmp = dpdk_pktmbuf_icmp(mbuf, sizeof(struct dpdk_eth) + header_len);
        icmp_len = dpdk_be_to_cpu_16(ipv4->total_length) - header_len;

        if (LIKEYLY(dpdk_icmp_cksum_verify(mbuf, icmp, icmp_len))) {
            if (icmp->icmp_type == DPDK_ICMP_TYPE_ECHO_REQUEST && icmp->icmp_code == DPDK_ICMP_CODE_ECHO_REQUEST) {
                _l3_icmp_echo_reply(mbuf, icmp);

                if (mbuf->pkt_len <= tls_dp->mtu) {
                    tx = &tls_tx[mbuf->port];
                    tx->data[tx->count++] = mbuf;
                } else {
                    _l3_icmp_fragment(mbuf);
                    tls_drop->data[tls_drop->count++] = mbuf;
                }
            }
        } else {
            tls_drop->data[tls_drop->count++] = mbuf;
        }
    }
}

static void _l3_ipv4_process(struct dpdk_mbuf *data[], int count)
{
    int ret = 0;
    bool *result = NULL;
    int ip_check_count = 0;
    struct dpdk_eth *eth = NULL;
    struct dpdk_ipv4 *ipv4 = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_headroom *headroom = NULL;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ipv4 = dpdk_pktmbuf_ipv4(mbuf);

        if ((mbuf->ol_flags & DPDK_RX_IP_CKSUM_GOOD) == 0) {
            if (dpdk_ipv4_cksum_verify(ipv4)) {
                tls_drop->data[tls_drop->count++] = mbuf;
                continue;
            }
        }

        if (UNLIKELY(ipv4->version != 4 || ipv4->time_to_live == 0)) {
            tls_drop->data[tls_drop->count++] = mbuf;
            continue;
        }

        if (dpdk_ipv4_mbuf_is_fragmented(ipv4)) {
            mbuf->l2_len = sizeof(struct dpdk_eth);
            mbuf->l3_len = dpdk_ipv4_header_len(ipv4);
            mbuf = dpdk_ipv4_mbuf_reassemble(tls_dp->frag_handle, mbuf, tls_dp->timer_cycles, ipv4);

            if (mbuf == NULL) {
                dpdk_ip_mbuf_table_add(tls_dp->frag_handle);
                if (dpdk_ip_mbuf_fail_exceed_thold(tls_dp->frag_handle)) {
                    dpdk_ip_mbuf_recall(tls_dp->frag_handle, tls_dp->timer_cycles);
                }

                continue;
            } else {
                dpdk_ip_mbuf_table_sub(tls_dp->frag_handle);
            }
        }

        tls_pending->data[ip_check_count++] = mbuf;
    }

    result = (bool *)tls_cache->data;
    _l3_ipv4_is_local_bulk(tls_pending->data, result, ip_check_count);

    for (int i = 0; i < ip_check_count; i++) {
        if (UNLIKELY(!result[i])) {
            tls_drop->data[tls_drop->count++] = tls_pending->data[i];
            continue;
        }

        mbuf = tls_pending->data[i];
        ipv4 = dpdk_pktmbuf_ipv4(mbuf);
        switch (ipv4->next_proto_id) {
        case IPPROTO_ICMP:
            if (LIKEYLY(mbuf->data_len >= (sizeof(struct dpdk_eth) + dpdk_ipv4_header_len(ipv4)))) {
                tls_icmp->data[tls_icmp->count++] = mbuf;
            } else {
                tls_drop->data[tls_drop->count++] = mbuf;
            }
            break;
        case IPPROTO_TCP:
        case IPPROTO_UDP:
        default:
            tls_drop->data[tls_drop->count++] = mbuf;
            break;
        }
    }

    if (tls_icmp->count != 0) {
        _l3_icmp_process((struct dpdk_mbuf **)tls_icmp->data, tls_icmp->count);
        tls_icmp->count = 0;
    }
}

void l3_refresh(void)
{
    l2_arp_refresh(_l3_ip_get_by_port);
}

enum IP_LOCAL_CLASS l3_ipv4_local_class(uint16_t port, uint32_t ip)
{
    int ret = 0;
    uint16_t idx = 0;
    uint64_t next_hop = 0;
    struct dpdk_fib *fib = NULL;
    struct ipv4_info *cur = NULL;
    struct list_head *head = NULL;
    struct ipv4_manage *manage = rcu_dereference(tls_th_cfg->ipv4_manage);

    idx = L3_IPv4_BUCKET_IDX(ip);
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

void l3_process(void)
{
    int ipv4_cnt = tls_ipv4->count;
    int ipv6_cnt = tls_ipv6->count;

    if (ipv4_cnt != 0) {
        _l3_ipv4_process((struct dpdk_mbuf **)tls_ipv4->data, ipv4_cnt);
        tls_ipv4->count = 0;
    }

    if (ipv6_cnt != 0) {
        // _l3_ipv6_do();
        tls_ipv6->count = 0;
    }
}

void *l3_thread_startup(int hw_numa_id, int nic_count)
{
    int ret = 0;
    static void *ptr[NUMA_MAX] = {NULL};

    dpdk_spinlock_lock(&s_spinlock[hw_numa_id]);

    if (ptr[hw_numa_id] == NULL) {
        _l3_conf_ipv4_manage_create(&ptr[hw_numa_id], nic_count, hw_numa_id);
    }

    dpdk_spinlock_unlock(&s_spinlock[hw_numa_id]);

    return ptr[hw_numa_id];
}

void l3_ipv4_manage_destroy(void *ptr)
{
    struct ipv4_manage *ipv4_manage = ptr;

    if (ptr == NULL) {
        return;
    }

    for (int i = 0; i < ipv4_manage->nic_count; i++) {
        if (ipv4_manage->fib[i] != NULL) {
            dpdk_fib_destroy(ipv4_manage->fib[i]);
            ipv4_manage->fib[i] = NULL;
        }
    }

    dpdk_free(ipv4_manage);
}