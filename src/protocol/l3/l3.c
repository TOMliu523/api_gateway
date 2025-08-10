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
#include "dpdk_icmp6.h"
#include "dpdk_common.h"
#include "dpdk_spinlock.h"

// Big Endian ip
#define L3_IPv4_BUCKET_IDX(x) ((x) >> 16)
#define L3_IPv4_BUCKET_MAX (1 << 16)

#define L3_IPv6_BUCKET_MAX (1 << 16)
#define L3_IPv6_ONLY_VERSION 0x60000000

struct ip4_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib *fib[DPDK_ETHPORT_MAX];
    /*
     * Each bucket contains multiple nodes, sorted in ascending order by IP address
     * (based on big-endian representation). If the IP addresses are equal,
     * the nodes are further sorted in ascending order by port number
     */
    struct list_head head[L3_IPv4_BUCKET_MAX];
    struct ip4_info *master[DPDK_ETHPORT_MAX];
    struct ip4_info store[L3_IPv4_BUCKET_MAX];
};

struct ip6_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib6 *fib[DPDK_ETHPORT_MAX];
    struct dpdk_hash *hash[DPDK_ETHPORT_MAX];
    struct ip6_info *master[DPDK_ETHPORT_MAX];
    struct ip6_info store[L3_IPv4_BUCKET_MAX];
};

static dpdk_spinlock_t s_spinlock[NUMA_MAX] = {0};
static struct dpdk_mac s_ipv6_multicast_mac = {
    .addr_bytes = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01},
};
static union dpdk_ip6_addr s_ipv6_multicast_ip = {
    .addr = {
        .a = {
            0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01,
        },
    },
};

int l3_conf_ndp_advertisement_gen(struct dpdk_mbuf *mbuf, uint16_t port, const union dpdk_ip6_addr *addr, const struct dpdk_mac *mac)
{
    struct dpdk_eth *eth = NULL;
    struct dpdk_ndp *ndp = NULL;
    struct dpdk_ndp_opt *opt = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;

    eth = dpdk_append(mbuf, sizeof(struct dpdk_eth) + sizeof(struct dpdk_ip6_hdr) + sizeof(struct dpdk_ndp), void *);
    if (UNLIKELY(eth == NULL)) {
        LOG_ERROR("There is not enough tailroom space in the last segment.");
        return -1;
    }

    mbuf->port = port;

    eth->dst_addr = s_ipv6_multicast_mac;
    eth->src_addr = *mac;
    eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_IP6);

    ip6hdr = (struct dpdk_ip6_hdr *)(eth + 1);
    ip6hdr->vtc_flow = dpdk_cpu_to_be_32(L3_IPv6_ONLY_VERSION);
    ip6hdr->payload_len = dpdk_cpu_to_be_16(sizeof(struct dpdk_ndp) + 8);
    ip6hdr->proto = IPPROTO_ICMPV6;
    ip6hdr->hop_limits = DPDK_LOCAL_HOP_LIMITS;
    ip6hdr->src_addr = addr->addr;
    ip6hdr->dst_addr = s_ipv6_multicast_ip.addr;

    ndp = (struct dpdk_ndp *)(ip6hdr + 1);
    ndp->icmp6_hdr.icmp6_type = DPDK_DNP_ADVERTISEMENT;
    ndp->icmp6_hdr.icmp6_code = 0;
    ndp->icmp6_hdr.icmp6_cksum = 0;
    ndp->icmp6_hdr.icmp6_dataun.un_data32[0] = 0;
    ndp->icmp6_hdr.icmp6_dataun.u_nd_advt.override = 1;
    ndp->target = *addr;

    opt = (struct dpdk_ndp_opt *)(ndp + 1);
    opt->type = 2;
    opt->len = 1;
    *(struct dpdk_mac *)opt->data = *mac;

    ndp->icmp6_hdr.icmp6_cksum = dpdk_icmp6_cksum(mbuf, ip6hdr, (void *)ndp - (void *)eth);

    return 0;
}

// Release the ipv4_manage structure.
static void _l3_conf_ip4_manage_destroy(struct ip4_manage *manage)
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

static void _l3_conf_ip6_manage_destroy(struct ip6_manage *manage)
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

static int _l3_conf_ip4_info_cmp(const void *key1, const void *key2, size_t len)
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

static int _l3_conf_ip4_manage_add(struct ip4_manage *manage, const struct ip4_info *one)
{
    int ret = 0;
    int nums = 0;
    uint32_t host_ip = 0;
    char ip_str[CACHE_LINE] = "";
    struct ip4_info *cur = NULL;
    struct list_head *prev = NULL;
    struct ip4_info *store = NULL;
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

static int _l3_conf_ip6_manage_add(struct ip6_manage *manage, const struct ip6_info *one)
{
    int ret = 0;
    int nums = 0;
    struct ip6_info *store = NULL;

    nums = manage->ip_count;
    store = &manage->store[nums];
    *store = *one;

    if (manage->master[one->port] == NULL && store->type == IP_MASTER) {
        manage->master[one->port] = store;
    }

    ret = dpdk_fib6_add(manage->fib[one->port], &store->ip6, store->mask, nums);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Failure dpdk_fib6_add: %s", strerror(-ret));
        return ERRCODE_INNER;
    }

    ret = dpdk_hash_add_kv(manage->hash[one->port], &one->ip6, store);
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
static int _l3_conf_ip4_manage_add_check(struct ip4_manage *manage, const struct ip4_info *info, int count)
{
    int ret = 0;
    uint8_t mask = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";
    const struct ip4_info *one = NULL;

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

static int _l3_conf_ip6_manage_add_check(struct ip6_manage *manage, const struct ip6_info *info, int count)
{
    int ret = 0;
    uint8_t mask = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";
    const struct ip6_info *one = NULL;

    if (UNLIKELY(manage == NULL || manage->ip_count == 0)) {
        return 0;
    }

    if (UNLIKELY(manage->ip_count + count > L3_IPv6_BUCKET_MAX)) {
        LOG_ERROR("Maximum supported IP address count(%d) exceeded", L3_IPv6_BUCKET_MAX);
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];

        ret = dpdk_fib6_lookup(manage->fib[one->port], &info->ip6, &next_hop, 1);
        if (UNLIKELY(ret != 0)) {
            LOG_ERROR("Inner error.");
            return ERRCODE_INNER;
        }

        if (UNLIKELY(ret == 0 && next_hop != DPDK_FIB6_DEFAULT && manage->store[next_hop].mask == one->mask)) {
            mask = one->mask;
            inet_ntop(AF_INET6, &one->ip6, ip_str, sizeof(ip_str));
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
static int _l3_conf_ip4_manage_del_check(struct ip4_manage *manage, const struct ip4_info *info, int count)
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

static int _l3_conf_ip6_manage_del_check(struct ip6_manage *manage, const struct ip6_info *info, int count)
{
    int ret = 0;
    bool hit = false;
    char ip_str[CACHE_LINE] = "";
    struct ip6_info *data = NULL;

    if (UNLIKELY(manage->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception: origin %d, delete %d", manage->ip_count, count);
        return ERRCODE_INNER;
    }

    // ret = dpdk_hash_lookup_data(manage->hash[info->port], (const void *)&info->ipv6, (void **)&data);
    if (UNLIKELY(ret != 0)) {
        inet_ntop(AF_INET6, &info->ip6, ip_str, sizeof(ip_str));
        LOG_ERROR("IP: %s, port: %d not exists", ip_str, info->port);
        return ERRCODE_IP_NOT_EXIST;
    }

    return 0;
}

// Initialize a fresh ipv4_manage object with default/empty values.
static int _l3_conf_ip4_manage_create(void **dst, int nic_count, int hw_numa_id)
{
    char name[CACHE_LINE] = "";
    struct ip4_manage *manage = NULL;

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
    _l3_conf_ip4_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _l3_conf_ip6_manage_create(void **dst, int nic_count, int hw_numa_id)
{
    struct ip6_manage *manage = NULL;

    manage = dpdk_malloc_numa(sizeof(*manage), hw_numa_id);
    if (UNLIKELY(manage == NULL)) {
        LOG_ERROR("HA NUMA(%d) OOM.", hw_numa_id);
        return ERRCODE_OOM;
    }

    memset(manage, 0, sizeof(*manage));

    manage->ip_count = 0;
    manage->nic_count = nic_count;

    for (int i = 0; i < nic_count; i++) {
        manage->hash[i] = dpdk_hash_create(L3_IPv6_BUCKET_MAX, sizeof(union dpdk_ip6_addr), hw_numa_id, _l3_conf_ip4_info_cmp);
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
    _l3_conf_ip6_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _l3_conf_ip4_manage_append(struct ip4_manage *dst, const struct ip4_manage *src, const struct ip4_info *info, int count)
{
    int ret = 0;

    for (int i = 0; i < src->ip_count; i++) {
        ret = _l3_conf_ip4_manage_add(dst, &src->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _l3_conf_ip4_manage_add(dst, &info[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _l3_conf_ip6_manage_append(struct ip6_manage *dst, const struct ip6_manage *src, const struct ip6_info *info, int count)
{
    int ret = 0;
    int nums = 0;

    for (int i = 0; i < src->ip_count; i++) {
        ret = _l3_conf_ip6_manage_add(dst, &src->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _l3_conf_ip6_manage_add(dst, &info[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _l3_conf_ip4_manage_delete(struct ip4_manage *dst, const struct ip4_manage *src, const struct ip4_info *info, int count)
{
    int ret = 0;
    int nums = 0;
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

        ret = _l3_conf_ip4_manage_add(dst, store);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _l3_conf_ip6_manage_delete(struct ip6_manage *dst, const struct ip6_manage *src, const struct ip6_info *info, int count)
{
    int ret = 0;
    int nums = 0;
    bool need_delete = false;
    const struct ip6_info *one = NULL;
    const struct ip6_info *store = NULL;

    for (int i = 0; i < src->ip_count; i++) {
        need_delete = false;
        store = &src->store[i];

        for (int j = 0; j < count; j++) {
            one = &info[j];
            if (dpdk_ip6_addr_eq(&one->ip6, &store->ip6) && one->port == store->port) {
                need_delete = true;
                break;
            }
        }

        if (need_delete) {
            continue;
        }

        ret = _l3_conf_ip6_manage_add(dst, store);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

bool l3_conf_ip4_manage_ip_is_local(const void *arg, uint32_t ip, uint8_t port)
{
    const struct ip4_info *cur = NULL;
    const struct list_head *head = NULL;
    const struct ip4_manage *ip4_manage = (const struct ip4_manage *)arg;

    if (arg == NULL) {
        return false;
    }

    head = &ip4_manage->head[L3_IPv4_BUCKET_IDX(ip)];
    list_for_each_entry(cur, head, node) {
        if (cur->ip == ip && cur->port == port) {
            return true;
        } else {
            continue;
        }
    }

    return false;
}

bool l3_conf_ip6_manage_ip_is_local(const void *arg, const union dpdk_ip6_addr *addr, uint8_t port)
{
    int ret = 0;
    const struct ip4_info *cur = NULL;
    const struct ip6_manage *ip6_manage = (const struct ip6_manage *)arg;

    if (arg == NULL) {
        return false;
    }

    // ret = dpdk_hash_lookup_bulk_data();

    return true;
}

int l3_conf_ip4_manage_create_and_append(void **dst, void *src, const struct ip4_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    const struct ip4_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    // Check whether the newly added IP address and subnet already exist
    ret = _l3_conf_ip4_manage_add_check(src, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    dpdk_spinlock_lock(&s_spinlock[hw_numa_id]);
    // Only create the ipv4_manage structure.
    ret = _l3_conf_ip4_manage_create(dst, one->nic_count, hw_numa_id);
    dpdk_spinlock_unlock(&s_spinlock[hw_numa_id]);

    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Add both new and existing data together
    ret = _l3_conf_ip4_manage_append(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ip4_manage_destroy(*dst);
        *dst = NULL;
        return ret;
    }

    return 0;
}

int l3_conf_ip4_manage_create_and_delete(void **dst, void *src, const struct ip4_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ip4_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || info == NULL || one->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, info: %p, count: %d).", dst, src, info, one->ip_count);
        return ERRCODE_INNER;
    }

    ret = _l3_conf_ip4_manage_del_check(src, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    // Only create the ipv4_manage structure.
    ret = _l3_conf_ip4_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ip4_manage_delete(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ip4_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

int l3_conf_ip6_manage_create_and_append(void **dst, void *src, const struct ip6_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ip6_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    ret = _l3_conf_ip6_manage_add_check(one, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ip6_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ip6_manage_append(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ip6_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

int l3_conf_ip6_manage_create_and_delete(void **dst, void *src, const struct ip6_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ip6_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    ret = _l3_conf_ip6_manage_del_check(one, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _l3_conf_ip6_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ip6_manage_destroy(*dst);
        return ret;
    }

    ret = _l3_conf_ip6_manage_delete(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _l3_conf_ip6_manage_destroy(*dst);
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
    return tlv_tx_offload[port] & DPDK_TX_IP_TX_IP_CKSUM;
}

static int _l3_ip4_get_by_port(uint32_t *ip, uint32_t target_ip, int port)
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

static INLINE void _l3_ip4_is_local_bulk(void *mbufs[], bool result[], int count)
{
    int nums = 0;
    uint16_t idx = 0;
    bool hit = false;
    struct ip4_info *cur = NULL;
    struct list_head *head = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;
    struct ip4_manage *manage = rcu_dereference(tlv_th_cfg->ip4_manage);

    for (int i = 0; i < count; i++) {
        mbuf = mbufs[i];
        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);

        idx = L3_IPv4_BUCKET_IDX(ip4hdr->dst_addr);
        head = &manage->head[idx];

        list_for_each_entry(cur, head, node) {
            if (ip4hdr->dst_addr == cur->ip) {
                if (mbuf->port == cur->port) {
                    hit = true;
                    result[nums++] = true;
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
    struct dpdk_ip4_hdr *ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);

    // mac
    SWAP(eth->src_addr, eth->dst_addr);

    // ip
    SWAP(ip4hdr->dst_addr, ip4hdr->src_addr);

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
    struct dpdk_eth *src_eth = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;

    src_eth = dpdk_pktmbuf_eth(mbuf);

    dpdk_pktmbuf_adj(mbuf, sizeof(struct dpdk_eth));

    tx = &tlv_tx[mbuf->port];
    rc = dpdk_ip4_mbuf_fragment(mbuf, &tx->data[tx->count], 64, tlv_dp->mtu, tlv_dp->pktmbuf_pool, tlv_dp->indirect_pool);
    if (UNLIKELY(rc < 0)) {
        return;
    }

    len = tx->count + rc;
    for (int i = tx->count; i < len; i++) {
        pkt = tx->data[i];

        pkt->port = mbuf->port;

        pkt->l2_len = sizeof(struct dpdk_eth);
        pkt->l3_len = sizeof(struct dpdk_ip4_hdr);

        eth = dpdk_pktmbuf_prepend(pkt, sizeof(struct dpdk_eth));
        eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_TYPE_IPV4);
        eth->dst_addr = src_eth->dst_addr;
        eth->src_addr = src_eth->src_addr;

        ip4hdr = dpdk_pktmbuf_ip4_hdr(pkt);
        ip4hdr->hdr_checksum = 0;
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
    struct dpdk_ip4_hdr *ip4hdr = NULL;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);
        header_len = dpdk_ip4_header_len(ip4hdr);
        icmp = dpdk_pktmbuf_icmp(mbuf, sizeof(struct dpdk_eth) + header_len);
        icmp_len = dpdk_be_to_cpu_16(ip4hdr->total_length) - header_len;

        if (LIKEYLY(dpdk_icmp_cksum_verify(mbuf, icmp, icmp_len))) {
            if (icmp->icmp_type == DPDK_ICMP_TYPE_ECHO_REQUEST && icmp->icmp_code == DPDK_ICMP_CODE_ECHO_REQUEST) {
                _l3_icmp_echo_reply(mbuf, icmp);

                if (mbuf->pkt_len <= tlv_dp->mtu) {
                    tx = &tlv_tx[mbuf->port];
                    tx->data[tx->count++] = mbuf;
                } else {
                    _l3_icmp_fragment(mbuf);
                    tlv_drop->data[tlv_drop->count++] = mbuf;
                }
            }
        } else {
            tlv_drop->data[tlv_drop->count++] = mbuf;
        }
    }
}

static void _l3_ip4_process(struct dpdk_mbuf *data[], int count)
{
    bool *result = NULL;
    int ip_check_count = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip4_hdr *ip4hdr = NULL;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);

        if (UNLIKELY(mbuf->data_len < sizeof(struct dpdk_eth) + sizeof(struct dpdk_ip4_hdr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY((mbuf->ol_flags & DPDK_RX_IP_CKSUM_GOOD) == 0)) {
            if (dpdk_ip4_header_cksum_verify(ip4hdr)) {
                tlv_drop->data[tlv_drop->count++] = mbuf;
                continue;
            }
        }

        if (UNLIKELY(ip4hdr->version != 4 || ip4hdr->time_to_live == 0)) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (dpdk_ip4_mbuf_is_fragmented(ip4hdr)) {
            mbuf->l2_len = sizeof(struct dpdk_eth);
            mbuf->l3_len = dpdk_ip4_header_len(ip4hdr);
            mbuf = dpdk_ip4_mbuf_reassemble(tlv_dp->frag_handle, mbuf, tlv_dp->timer_cycles, ip4hdr);

            if (mbuf == NULL) {
                dpdk_ip_mbuf_table_add(tlv_dp->frag_handle);
                if (dpdk_ip_mbuf_fail_exceed_thold(tlv_dp->frag_handle)) {
                    dpdk_ip_mbuf_recall(tlv_dp->frag_handle, tlv_dp->timer_cycles);
                }

                continue;
            } else {
                dpdk_ip_mbuf_table_sub(tlv_dp->frag_handle);
            }
        }

        tlv_pending->data[ip_check_count++] = mbuf;
    }

    result = (bool *)tlv_cache->data;
    _l3_ip4_is_local_bulk(tlv_pending->data, result, ip_check_count);

    for (int i = 0; i < ip_check_count; i++) {
        if (UNLIKELY(!result[i])) {
            tlv_drop->data[tlv_drop->count++] = tlv_pending->data[i];
            continue;
        }

        mbuf = tlv_pending->data[i];
        ip4hdr = dpdk_pktmbuf_ip4_hdr(mbuf);
        switch (ip4hdr->next_proto_id) {
        case IPPROTO_ICMP:
            if (LIKEYLY(mbuf->data_len >= (sizeof(struct dpdk_eth) + dpdk_ip4_header_len(ip4hdr)))) {
                tlv_icmp->data[tlv_icmp->count++] = mbuf;
            } else {
                tlv_drop->data[tlv_drop->count++] = mbuf;
            }
            break;
        case IPPROTO_TCP:
        case IPPROTO_UDP:
        default:
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        }
    }

    if (tlv_icmp->count != 0) {
        _l3_icmp_process((struct dpdk_mbuf **)tlv_icmp->data, tlv_icmp->count);
        tlv_icmp->count = 0;
    }
}

static void _l3_ip6_process(struct dpdk_mbuf *data[], int count)
{
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];

        if (UNLIKELY(mbuf->data_len < sizeof(struct dpdk_eth) + sizeof(struct dpdk_ip6_hdr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);
        if (UNLIKELY(ip6hdr->version != 6)) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }
    }
}

void l3_refresh(void)
{
    l2_arp_refresh(_l3_ip4_get_by_port);
}

enum IP_LOCAL_CLASS l3_ipv4_local_class(uint16_t port, uint32_t ip)
{
    int ret = 0;
    uint16_t idx = 0;
    uint64_t next_hop = 0;
    struct dpdk_fib *fib = NULL;
    struct ip4_info *cur = NULL;
    struct list_head *head = NULL;
    struct ip4_manage *manage = rcu_dereference(tlv_th_cfg->ip4_manage);

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
    int ipv4_cnt = tlv_ip4->count;
    int ipv6_cnt = tlv_ip6->count;

    if (ipv4_cnt != 0) {
        _l3_ip4_process((struct dpdk_mbuf **)tlv_ip4->data, ipv4_cnt);
        tlv_ip4->count = 0;
    }

    if (ipv6_cnt != 0) {
        _l3_ip6_process((struct dpdk_mbuf **)tlv_ip6->data, ipv6_cnt);
        tlv_ip6->count = 0;
    }
}

void *l3_thread_startup(int hw_numa_id, int nic_count)
{
    int ret = 0;
    static void *ptr[NUMA_MAX] = {NULL};

    dpdk_spinlock_lock(&s_spinlock[hw_numa_id]);

    if (ptr[hw_numa_id] == NULL) {
        _l3_conf_ip4_manage_create(&ptr[hw_numa_id], nic_count, hw_numa_id);
    }

    dpdk_spinlock_unlock(&s_spinlock[hw_numa_id]);

    return ptr[hw_numa_id];
}

void l3_ip4_manage_destroy(void *ptr)
{
    _l3_conf_ip4_manage_destroy(ptr);
}

void l3_ip6_manage_destroy(void *ptr)
{
    _l3_conf_ip6_manage_destroy(ptr);
}