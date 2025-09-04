/*****************************************************************************
 * filename: ip6.c
 * function:
 * description:
 *****************************************************************************/

#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "l2.h"
#include "ip6.h"
#include "list.h"
#include "type.h"
#include "macro.h"
#include "errcode.h"
#include "protocol.h"
#include "dpdk_rcu.h"
#include "dpdk_hash.h"
#include "dpdk_core.h"
#include "dpdk_fib6.h"
#include "dpdk_icmp6.h"
#include "dpdk_bitops.h"

#define IP6_BUCKET_MAX (1 << 16)
#define IP6_ONLY_VERSION 0x60000000

#define NDP_GC_MAX 64
#define NDP_ITEM_MAX (65536)
#define NDP_HASH_BUCKET_MAX (65536)

#define NDP_SOLICT_NO_OPT_LEN (sizeof(struct dpdk_ndp_hdr))
#define NDP_SOLICT_HAS_OPT_LEN (sizeof(struct dpdk_ndp_hdr) + 8)
#define NDP_AD_NO_OPT_LEN (sizeof(struct dpdk_ndp_hdr))
#define NDP_AD_HAS_OPT_LEN (sizeof(struct dpdk_ndp_hdr) + 8)

#define NDP_TIMEOUT_DEFAULT (20 * 60)

#define NDP_TIME_TO_SEND(c, t) ((t) >= ((c)->expire_time - (5 - (c)->retry) * 60))

enum NDP_FLAGS {
    NDP_FLAGS_MANUAL,
    NDP_FLAGS_COMPLETE,
    NDP_FLAGS_INCOMPLETE,
};

struct ndp_item {
    union {
        struct {
            struct list_head node;
            union {
                struct list_head man_node;
                struct list_head lru_node;
                struct list_head incomplete_node;
            };
            struct dpdk_ip6_addr target;
            struct dpdk_mac mac;
            uint16_t port : 8;
            uint16_t flags : 4;
            uint16_t retry : 4;
            uint32_t expire_time;
            uint32_t send_time;
        };
        uint8_t cache_line[CACHE_LINE];
    };
} ALIGN_CACHE_LINE;

struct ndp_table {
    void *pool;
    int nic_count;
    int ndp_count;
    uint32_t timeout;
    struct {
        struct list_head man_head;
        struct list_head lru_head;
    };
    struct dpdk_hash *hash[DPDK_ETHPORT_MAX];
};

struct ip6_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib6 *fib[DPDK_ETHPORT_MAX];
    struct dpdk_hash *hash[DPDK_ETHPORT_MAX];
    struct ip6_info *master[DPDK_ETHPORT_MAX];
    struct ip6_info store[IP6_BUCKET_MAX];
};

static struct dpdk_mac s_ipv6_multicast_mac = {
    .addr_bytes = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01},
};
static struct dpdk_ip6_addr s_ipv6_multicast_ip = {
    .a = {
        0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01,
    },
};

static void _ip6_conf_manage_destroy(struct ip6_manage *manage)
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

static int _ip6_conf_info_cmp(const void *first, const void *second, size_t len)
{
    return memcmp(first, second, len);
}

static int _ip6_conf_manage_del_check(struct ip6_manage *manage, const struct ip6_info *info, int count)
{
    int ret = 0;
    char ip_str[CACHE_LINE] = "";
    struct ip6_info *data = NULL;
    const struct ip6_info *one = NULL;

    if (UNLIKELY(manage->ip_count - count < 0)) {
        LOG_ERROR("Parameter exception: origin %d, delete %d", manage->ip_count, count);
        return ERRCODE_INNER;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];
        ret = dpdk_hash_lookup(manage->hash[one->port], (const void *)&one->addr, (void **)&data);
        if (UNLIKELY(ret < 0)) {
            inet_ntop(AF_INET6, &one->addr, ip_str, sizeof(ip_str));
            LOG_ERROR("IP: %s, port: %d not exists", ip_str, one->port);
            return ERRCODE_IP_NOT_EXIST;
        }
    }

    return 0;
}

static int _ip6_conf_manage_add(struct ip6_manage *manage, const struct ip6_info *one)
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

    ret = dpdk_fib6_add(manage->fib[one->port], &store->addr, store->mask, nums);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Failure dpdk_fib6_add: %s", strerror(-ret));
        return ERRCODE_INNER;
    }

    ret = dpdk_hash_add_kv(manage->hash[one->port], &one->addr, store);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Failure dpdk_hash_add_kv: %s", strerror(-rte_errno));
        return ERRCODE_INNER;
    }

    manage->ip_count += 1;
    return 0;
}

static int _ip6_conf_manage_add_check(struct ip6_manage *manage, const struct ip6_info *info, int count)
{
    int ret = 0;
    uint8_t mask = 0;
    uint64_t next_hop = 0;
    char ip_str[CACHE_LINE] = "";
    const struct ip6_info *one = NULL;

    if (UNLIKELY(manage == NULL || manage->ip_count == 0)) {
        return 0;
    }

    if (UNLIKELY(manage->ip_count + count > IP6_BUCKET_MAX)) {
        LOG_ERROR("Maximum supported IP address count(%d) exceeded", IP6_BUCKET_MAX);
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];

        ret = dpdk_fib6_lookup(manage->fib[one->port], &one->addr, &next_hop, 1);
        if (UNLIKELY(ret != 0)) {
            LOG_ERROR("Inner error.");
            return ERRCODE_INNER;
        }

        if (UNLIKELY(ret == 0 && next_hop != DPDK_FIB6_DEFAULT && manage->store[next_hop].mask == one->mask)) {
            mask = one->mask;
            inet_ntop(AF_INET6, &one->addr, ip_str, sizeof(ip_str));
            LOG_ERROR("IP address conflict - another IP(%s/%d) in the same subnet is already configured.", ip_str, mask);
            return ERRCODE_SUBNET_EXIST;
        }
    }

    return 0;
}

static int _ip6_conf_manage_create(void **dst, int nic_count, int hw_numa_id)
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
        manage->hash[i] = dpdk_hash_create(IP6_BUCKET_MAX, sizeof(struct dpdk_ip6_addr), hw_numa_id, _ip6_conf_info_cmp);
        if (UNLIKELY(manage->hash[i] == NULL)) {
            goto _quit;
        }
    }

    for (int i = 0; i < nic_count; i++) {
        manage->fib[i] = dpdk_fib6_create(hw_numa_id, IP6_BUCKET_MAX);
        if (UNLIKELY(manage->fib[i] == NULL)) {
            goto _quit;
        }
    }

    *dst = manage;
    return 0;

_quit:
    _ip6_conf_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _ip6_conf_manage_append(struct ip6_manage *dst, const struct ip6_manage *src, const struct ip6_info *info, int count)
{
    int ret = 0;

    for (int i = 0; i < src->ip_count; i++) {
        ret = _ip6_conf_manage_add(dst, &src->store[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    for (int i = 0; i < count; i++) {
        ret = _ip6_conf_manage_add(dst, &info[i]);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

static int _ip6_conf_manage_delete(struct ip6_manage *dst, const struct ip6_manage *src, const struct ip6_info *info, int count)
{
    int ret = 0;
    bool need_delete = false;
    const struct ip6_info *one = NULL;
    const struct ip6_info *store = NULL;

    for (int i = 0; i < src->ip_count; i++) {
        need_delete = false;
        store = &src->store[i];

        for (int j = 0; j < count; j++) {
            one = &info[j];
            if (dpdk_ip6_addr_eq(&one->addr, &store->addr) && one->port == store->port) {
                need_delete = true;
                break;
            }
        }

        if (need_delete) {
            continue;
        }

        ret = _ip6_conf_manage_add(dst, store);
        if (UNLIKELY(ret != 0)) {
            return ret;
        }
    }

    return 0;
}

void ip6_conf_manage_destroy(void *ptr)
{
    _ip6_conf_manage_destroy(ptr);
}

bool ip6_conf_manage_ip_is_local(const void *arg, const struct dpdk_ip6_addr *addr, uint8_t port)
{
    int ret = 0;
    void *data = NULL;
    const struct ip6_manage *manage = (const struct ip6_manage *)arg;

    if (manage == NULL) {
        return false;
    }

    ret = dpdk_hash_lookup(manage->hash[port], addr, &data);
    if (ret != 0) {
        return false;
    }

    return true;
}

int ip6_ndp_na_mcast_gen(struct dpdk_mbuf *mbuf, uint16_t port, const struct dpdk_ip6_addr *addr, const struct dpdk_mac *mac)
{
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_ndp_hdr *ndp = NULL;
    struct dpdk_ndp_opt *opt = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;

    const uint16_t l2_len = sizeof(struct dpdk_eth_hdr);
    const uint16_t l3_len = sizeof(struct dpdk_ip6_hdr);
    const uint16_t l4_len = sizeof(struct dpdk_ndp_hdr) + 8;

    eth = dpdk_append(mbuf, l2_len + l3_len + l4_len, void *);
    if (UNLIKELY(eth == NULL)) {
        LOG_ERROR("There is not enough tailroom space in the last segment.");
        return -1;
    }

    mbuf->port = port;

    eth->dst_addr = s_ipv6_multicast_mac;
    eth->src_addr = *mac;
    eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_IP6);

    ip6hdr = (struct dpdk_ip6_hdr *)(eth + 1);
    ip6hdr->vtc_flow = dpdk_cpu_to_be_32(IP6_ONLY_VERSION);
    ip6hdr->payload_len = dpdk_cpu_to_be_16(sizeof(struct dpdk_ndp_hdr) + 8);
    ip6hdr->proto = IPPROTO_ICMPV6;
    ip6hdr->hop_limits = DPDK_LOCAL_HOP_LIMITS;
    ip6hdr->src_addr = *addr;
    ip6hdr->dst_addr = s_ipv6_multicast_ip;

    ndp = (struct dpdk_ndp_hdr *)(ip6hdr + 1);
    ndp->icmp6_hdr.icmp6_type = DPDK_NDP_ADVERT;
    ndp->icmp6_hdr.icmp6_code = 0;
    ndp->icmp6_hdr.icmp6_cksum = 0;
    ndp->icmp6_hdr.icmp6_dataun.un_data32[0] = 0;
    ndp->target = *addr;

    opt = (struct dpdk_ndp_opt *)(ndp + 1);
    opt->type = 2;
    opt->len = 1;
    *(struct dpdk_mac *)opt->data = *mac;

    ndp->icmp6_hdr.icmp6_cksum = dpdk_icmp6_cksum(mbuf, ip6hdr, (void *)ndp - (void *)eth);

    return 0;
}

int ip6_conf_manage_create_and_append(void **dst, void *src, const struct ip6_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ip6_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    ret = _ip6_conf_manage_add_check(one, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _ip6_conf_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _ip6_conf_manage_append(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _ip6_conf_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

int ip6_conf_manage_create_and_delete(void **dst, void *src, const struct ip6_info *info, int count, int hw_numa_id)
{
    int ret = 0;
    struct ip6_manage *one = src;

    if (UNLIKELY(dst == NULL || src == NULL || count < 0)) {
        LOG_ERROR("Parameter exception(dst: %p, src: %p, count: %d).", dst, src, count);
        return ERRCODE_INNER;
    }

    ret = _ip6_conf_manage_del_check(one, info, count);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    ret = _ip6_conf_manage_create(dst, one->nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        _ip6_conf_manage_destroy(*dst);
        return ret;
    }

    ret = _ip6_conf_manage_delete(*dst, one, info, count);
    if (UNLIKELY(ret != 0)) {
        _ip6_conf_manage_destroy(*dst);
        return ret;
    }

    return 0;
}

// Config plane interface
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
// Data plane interface

static INLINE bool _ip6_is_local(int port, const void *key)
{
    int ret = 0;
    void *data = NULL;
    struct ip6_manage *manage = rcu_dereference(tlv_th_cfg->ip6_manage);

    ret = dpdk_hash_lookup(manage->hash[port], key, &data);
    return (ret >= 0) ? true : false;
}

static INLINE int _ip6_is_local_bulk(void *data[], uint64_t result[], int count)
{
    struct dpdk_mbuf *mbuf = NULL;

    void **keys = tlv_cache1->data;
    void **outs = tlv_cache2->data;

    struct dpdk_ip6_hdr *ip6hdr = NULL;
    struct ip6_manage *manage = rcu_dereference(tlv_th_cfg->ip6_manage);

    UNROLL_LOOP_8(i, count, {
        mbuf = data[i];
        ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);

        keys[i] = &ip6hdr->dst_addr;
    });

    // The caller must ensure that the incoming mbuf belongs to a single port.
    mbuf = data[0];
    return dpdk_hash_lookup_bulk(manage->hash[mbuf->port], (const void **)keys, count, result, outs);
}

static INLINE bool _ip6_port_ip_get(int port, struct dpdk_ip6_addr *addr)
{
    struct ip6_manage *manage = rcu_dereference(tlv_th_cfg->ip6_manage);

    if (UNLIKELY(manage->master[port] == NULL)) {
        return false;
    }

    *addr = manage->master[port]->addr;
    return true;
}

static INLINE struct dpdk_mbuf *_ip6_ndp_dad_na_gen(const struct dpdk_mbuf *mbuf)
{
    int ret = 0;
    struct dpdk_mac mac;
    int port = mbuf->port;
    struct dpdk_ip6_addr addr;
    struct dpdk_mbuf *one = NULL;

    ret = dpdk_pktmbuf_pop(tlv_dp->pktmbuf_pool, (void **)&one, 1);
    if (UNLIKELY(ret != 0)) {
        return NULL;
    }

    if (UNLIKELY(!_ip6_port_ip_get(port, &addr))) {
        dpdk_pktmbuf_push((void **)&one, 1);
        return NULL;
    }

    l2_thread_port_mac(port, &mac);
    ret = ip6_ndp_na_mcast_gen(one, port, &addr, &mac);
    if (UNLIKELY(ret != 0)) {
        dpdk_pktmbuf_push((void **)&one, 1);
        return NULL;
    }

    return one;
}

static INLINE struct dpdk_mbuf *
_ip6_ndp_nud_ns_gen(struct dpdk_mbuf *mbuf, int port, struct dpdk_ip6_addr *dst_addr, struct dpdk_mac *dst_mac)
{
    struct dpdk_eth_hdr *ethhdr = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;
    struct dpdk_ndp_hdr *ndphdr = NULL;
    struct dpdk_ndp_opt *ndpopt = NULL;

    mbuf->port = port;

    ethhdr = dpdk_append(mbuf, DPDK_NDP_BASE_LEN + DPDK_NDP_SRC_LINK_OPT_LEN, struct dpdk_eth_hdr *);
    if (UNLIKELY(ethhdr == NULL)) {
        return NULL;
    }

    ethhdr->dst_addr = *dst_mac;
    l2_thread_port_mac(port, &ethhdr->src_addr);
    ethhdr->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_IP6);

    ip6hdr = (struct dpdk_ip6_hdr *)(ethhdr + 1);
    ip6hdr->vtc_flow = dpdk_cpu_to_be_32(IP6_ONLY_VERSION);
    ip6hdr->payload_len = dpdk_cpu_to_be_16(sizeof(*ndphdr) + DPDK_NDP_SRC_LINK_OPT_LEN);
    ip6hdr->proto = IPPROTO_ICMPV6;
    ip6hdr->hop_limits = DPDK_LOCAL_HOP_LIMITS;
    if (UNLIKELY(!_ip6_port_ip_get(port, &ip6hdr->src_addr))) {
        return NULL;
    }
    ip6hdr->dst_addr = *dst_addr;

    ndphdr = (struct dpdk_ndp_hdr *)(ip6hdr + 1);
    ndphdr->icmp6_hdr.icmp6_type = DPDK_NDP_SOLICT;
    ndphdr->icmp6_hdr.icmp6_code = 0;
    ndphdr->icmp6_hdr.icmp6_cksum = 0;
    ndphdr->icmp6_hdr.icmp6_dataun.un_data32[0] = 0;
    ndphdr->target = *dst_addr;

    ndpopt = (struct dpdk_ndp_opt *)(ndphdr + 1);
    ndpopt->type = DPDK_NDP_SRC_LINK_OPT;
    ndpopt->len = 1;
    *(struct dpdk_mac *)ndpopt->data = ethhdr->src_addr;

    ndphdr->icmp6_hdr.icmp6_cksum = dpdk_icmp6_cksum(mbuf, ip6hdr, sizeof(*ethhdr) + sizeof(*ip6hdr));
    return mbuf;
}

static INLINE struct dpdk_mbuf *_ip6_ndp_nud_na_gen(const struct dpdk_mbuf *mbuf)
{
    int ret = 0;
    struct dpdk_mbuf *one = NULL;
    struct dpdk_eth_hdr *ethhdr = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;
    struct dpdk_ndp_hdr *ndphdr = NULL;
    struct dpdk_ndp_opt *ndpopt = NULL;

    struct dpdk_eth_hdr *eth = DPDK_HEADROOM(mbuf)->l2;
    struct dpdk_ip6_hdr *ip6 = DPDK_HEADROOM(mbuf)->l3;
    struct dpdk_ndp_hdr *ndp = DPDK_HEADROOM(mbuf)->l4;

    ret = dpdk_pktmbuf_pop(tlv_dp->pktmbuf_pool, (void **)&one, 1);
    if (UNLIKELY(ret != 0)) {
        return NULL;
    }

    ethhdr = dpdk_append(one, DPDK_NDP_BASE_LEN + DPDK_NDP_DST_LINK_OPT_LEN, struct dpdk_eth_hdr *);
    if (UNLIKELY(ethhdr == NULL)) {
        dpdk_pktmbuf_push((void **)&one, 1);
        return NULL;
    }

    ethhdr->dst_addr = eth->src_addr;
    ethhdr->src_addr = eth->dst_addr;
    ethhdr->ether_type = eth->ether_type;

    ip6hdr = (struct dpdk_ip6_hdr *)(ethhdr + 1);
    ip6hdr->vtc_flow = ip6->vtc_flow;
    ip6hdr->payload_len = dpdk_cpu_to_be_16(sizeof(*ndphdr) + DPDK_NDP_DST_LINK_OPT_LEN);
    ip6hdr->proto = ip6->proto;
    ip6hdr->hop_limits = DPDK_LOCAL_HOP_LIMITS;
    ip6hdr->src_addr = ip6->dst_addr;
    ip6hdr->dst_addr = ip6->src_addr;

    ndphdr = (struct dpdk_ndp_hdr *)(ip6hdr + 1);
    ndphdr->icmp6_hdr.icmp6_type = DPDK_NDP_ADVERT;
    ndphdr->icmp6_hdr.icmp6_code = 0;
    ndphdr->icmp6_hdr.icmp6_cksum = 0;
    ndphdr->icmp6_hdr.icmp6_dataun.un_data32[0] = dpdk_cpu_to_be_32(DPDK_NDP_NA_FLAG_SOLICT | DPDK_NDP_NA_FLAG_OVERRIDE);
    ndphdr->target = ndp->target;

    ndpopt = (struct dpdk_ndp_opt *)(ndphdr + 1);
    ndpopt->type = DPDK_NDP_DST_LINK_OPT;
    ndpopt->len = 1;
    *(struct dpdk_mac *)ndpopt->data = ethhdr->src_addr;

    ndphdr->icmp6_hdr.icmp6_cksum = dpdk_icmp6_cksum(one, ip6hdr, sizeof(*ethhdr) + sizeof(*ip6hdr));
    return one;
}

static INLINE struct dpdk_mbuf *_ip6_ndp_ns_mcast_na_ucast_gen(const struct dpdk_mbuf *mbuf)
{
    int ret = 0;
    struct dpdk_mbuf *one = NULL;
    struct dpdk_eth_hdr *ethhdr = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;
    struct dpdk_ndp_hdr *ndphdr = NULL;
    struct dpdk_ndp_opt *ndpopt = NULL;

    struct dpdk_eth_hdr *eth = DPDK_HEADROOM(mbuf)->l2;
    struct dpdk_ip6_hdr *ip6 = DPDK_HEADROOM(mbuf)->l3;
    struct dpdk_ndp_hdr *ndp = DPDK_HEADROOM(mbuf)->l4;

    size_t len = sizeof(*ethhdr) + sizeof(*ip6hdr) + sizeof(*ndphdr) + DPDK_NDP_DST_LINK_OPT_LEN;

    ret = dpdk_pktmbuf_pop(tlv_dp->pktmbuf_pool, (void **)&one, 1);
    if (UNLIKELY(ret != 0)) {
        return NULL;
    }

    ethhdr = dpdk_append(one, len, struct dpdk_eth_hdr *);
    if (UNLIKELY(ethhdr == 0)) {
        dpdk_pktmbuf_push((void **)&one, 1);
        return NULL;
    }

    ip6hdr = (struct dpdk_ip6_hdr *)(ethhdr + 1);
    if (UNLIKELY(!_ip6_port_ip_get(mbuf->port, &ip6hdr->src_addr))) {
        dpdk_pktmbuf_push((void **)&one, 1);
        return NULL;
    }

    ethhdr->dst_addr = eth->src_addr;
    l2_thread_port_mac(mbuf->port, &ethhdr->src_addr);
    ethhdr->ether_type = eth->ether_type;

    ip6hdr->vtc_flow = ip6->vtc_flow;
    ip6hdr->payload_len = dpdk_cpu_to_be_16(sizeof(*ndphdr) + DPDK_NDP_DST_LINK_OPT_LEN);
    ip6hdr->proto = ip6->proto;
    ip6hdr->hop_limits = DPDK_LOCAL_HOP_LIMITS;
    ip6hdr->dst_addr = ip6->src_addr;

    ndphdr = (struct dpdk_ndp_hdr *)(ip6hdr + 1);
    ndphdr->icmp6_hdr.icmp6_type = DPDK_NDP_ADVERT;
    ndphdr->icmp6_hdr.icmp6_code = 0;
    ndphdr->icmp6_hdr.icmp6_cksum = 0;
    ndphdr->icmp6_hdr.icmp6_dataun.un_data32[0] = dpdk_cpu_to_be_32(DPDK_NDP_NA_FLAG_SOLICT | DPDK_NDP_NA_FLAG_OVERRIDE);
    ndphdr->target = ndp->target;

    ndpopt = (struct dpdk_ndp_opt *)(ndphdr + 1);
    ndpopt->type = DPDK_NDP_DST_LINK_OPT;
    ndpopt->len = 1;
    *(struct dpdk_mac *)ndpopt->data = ethhdr->src_addr;

    ndphdr->icmp6_hdr.icmp6_cksum = dpdk_icmp6_cksum(one, ip6hdr, sizeof(*ethhdr) + sizeof(*ip6hdr));
    return one;
}

static INLINE void _ip6_icmp_fragment(struct dpdk_mbuf *mbuf, uint16_t mtu)
{
    int rc = 0;
    int len = 0;
    int tx_count = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_mbuf *pkt = NULL;
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_eth_hdr *src_eth = &DPDK_HEADROOM(mbuf)->ethhdr;
    struct dpdk_mac src_addr = src_eth->src_addr;
    struct dpdk_mac dst_addr = src_eth->dst_addr;

    dpdk_pktmbuf_adj(mbuf, sizeof(struct dpdk_eth_hdr));

    tx = &tlv_tx[mbuf->port];
    tx_count = tx->count;
    rc = dpdk_ip6_mbuf_fragment(mbuf, &tx->data[tx->count], 64, mtu, tlv_dp->pktmbuf_pool, tlv_dp->indirect_pool);
    if (UNLIKELY(rc < 0)) {
        return;
    }

    len = tx_count + rc;
    for (int i = tx_count; i < len; i++) {
        pkt = tx->data[i];

        pkt->l2_len = sizeof(struct dpdk_eth_hdr);
        pkt->l3_len = sizeof(struct dpdk_ip6_hdr) + sizeof(struct dpdk_ip6_frag_ext);

        eth = dpdk_pktmbuf_prepend(pkt, pkt->l2_len);
        if (UNLIKELY(eth == NULL)) {
            goto _quit;
        }

        eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_IP6);
        eth->src_addr = dst_addr;
        eth->dst_addr = src_addr;
    }

    tx->count = len;
    return;

_quit:
    for (int i = 0; i < rc; i++) {
        tlv_drop->data[tlv_drop->count++] = tx->data[tx_count + i];
    }
    return;
}

static INLINE int _ip6_icmp_reply(struct dpdk_mbuf *mbuf)
{
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;
    struct dpdk_icmp6_hdr *icmp6hdr = NULL;

    icmp6hdr = dpdk_pktmbuf_icmp6_hdr(mbuf);
    if (UNLIKELY(icmp6hdr->icmp6_code != 0)) {
        return -1;
    }

    ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);
    if (UNLIKELY(!dpdk_icmp6_cksum_verify(mbuf, ip6hdr))) {
        return -1;
    }

    eth = dpdk_pktmbuf_eth(mbuf);
    SWAP(eth->src_addr, eth->dst_addr);

    SWAP(ip6hdr->src_addr, ip6hdr->dst_addr);

    dpdk_icmp6_echo_reply_ckcum(icmp6hdr);
    return 0;
}

static INLINE bool _ip6_target_is_solict_addr(const struct dpdk_ip6_addr *ip6dst, const struct dpdk_ip6_addr *ndpdst)
{
    struct dpdk_ip6_addr target;

    if (UNLIKELY(dpdk_ip6_addr_is_mcast(ndpdst))) {
        return false;
    }

    dpdk_ip6_solnode_from_addr(&target, ndpdst);
    return dpdk_ip6_addr_eq(ip6dst, &target);
}

static INLINE void _ip6_ndp_init(struct ndp_item *item, const struct dpdk_ip6_addr *addr,
                                 const struct dpdk_mac *mac, int port, uint32_t off_time)
{
    INIT_LIST_HEAD(&item->node);
    INIT_LIST_HEAD(&item->lru_node);
    item->target = *addr;
    item->mac = *mac;
    item->port = port;
    item->flags = NDP_FLAGS_COMPLETE;
    item->retry = 0;
    item->expire_time = off_time;
    item->send_time = 0;
}

static INLINE void _ip6_ndp_part_init(struct ndp_item *item, bool override, const struct dpdk_mac *mac, uint32_t off_time)
{
    if (override) {
        item->mac = *mac;
    }

    item->flags = NDP_FLAGS_COMPLETE;
    item->retry = 0;
    item->expire_time = off_time;
    item->send_time = 0;
}

static INLINE bool _ip6_ndp_table_update(int port, const struct dpdk_ip6_addr *addr, const struct dpdk_mac *mac, bool override)
{
    int ret = 0;
    struct ndp_item *data = NULL;
    struct ndp_table *table = ((struct proto_header *)tlv_dp->protocol)->nt;

    static __thread int s_ndp_cache_count = 0;
    static __thread void *s_ndp_cache[NDP_GC_MAX] = {NULL};

    ret = dpdk_hash_lookup(table->hash[port], addr, (void **)&data);
    if (ret >= 0) {
        _ip6_ndp_part_init(data, override, mac, table->timeout + tlv_dp->off_time);
        list_del(&data->lru_node);
        list_add(&data->lru_node, &table->lru_head);
        return true;
    } else if (ret == -ENOENT) {
        if (s_ndp_cache_count == 0) {
            ret = dpdk_mempool_pop(table->pool, s_ndp_cache, ARR_NUMS(s_ndp_cache));
            if (UNLIKELY(ret == 0)) {
                s_ndp_cache_count = ARR_NUMS(s_ndp_cache);
            } else {
                ret = dpdk_mempool_pop(table->pool, s_ndp_cache, 1);
                if (LIKELY(ret == 0)) {
                    s_ndp_cache_count = 1;
                }
            }

            if (UNLIKELY(s_ndp_cache_count == 0)) {
                LOG_ERROR("The NDP table has reached its limit; no new entries can be added.");
                return false;
            }
        }

        data = s_ndp_cache[s_ndp_cache_count - 1];

        _ip6_ndp_init(data, addr, mac, port, table->timeout + tlv_dp->off_time);
        list_add(&data->lru_node, &table->lru_head);
        ret = dpdk_hash_add_kv(table->hash[port], addr, data);
        if (UNLIKELY(ret != 0)) {
            return false;
        }

        table->ndp_count += 1;
        s_ndp_cache_count -= 1;
        return true;
    } else {
        return false;
    }
}

static INLINE void _ip6_ndp_table_update_and_notify(struct dpdk_mbuf *mbuf, const struct dpdk_ip6_addr *addr,
                                                    const struct dpdk_mac *mac, bool override, int ndp_type)
{
    bool flag = _ip6_ndp_table_update(mbuf->port, addr, mac, override);
    if (flag) {
        DPDK_HEADROOM(mbuf)->type = ndp_type;
        tlv_notify->data[tlv_notify->count++] = mbuf;
    } else {
        tlv_drop->data[tlv_drop->count++] = mbuf;
    }
}

static INLINE void _ip6_ndp_solict_process(struct dpdk_mbuf *mbuf)
{
    uint16_t payload_len = 0;
    struct dpdk_mbuf *ad_mbuf = NULL;
    struct dpdk_ip6_hdr *ip6hdr = DPDK_HEADROOM(mbuf)->l3;
    struct dpdk_ndp_hdr *ndp = DPDK_HEADROOM(mbuf)->l4;

    if (UNLIKELY(!dpdk_icmp6_cksum_verify(mbuf, ip6hdr))) {
        goto _quit;
    }

    if (UNLIKELY(ip6hdr->hop_limits != UINT8_MAX)) {
        goto _quit;
    }

    if (UNLIKELY(ndp->icmp6_hdr.icmp6_code != 0)) {
        goto _quit;
    }

    payload_len = dpdk_be_to_cpu_16(ip6hdr->payload_len);
    switch (payload_len) {
    case NDP_SOLICT_NO_OPT_LEN: {
        uint16_t port = mbuf->port;
        struct pkt_tx *tx = &tlv_tx[port];

        if (LIKELY(!dpdk_ip6_addr_is_unspec(&ip6hdr->src_addr))) {
            goto _quit;
        }

        if (UNLIKELY(!_ip6_target_is_solict_addr(&ip6hdr->dst_addr, &ndp->target))) {
            goto _quit;
        }

        if (UNLIKELY(!_ip6_is_local(port, (const void *)&ndp->target))) {
            goto _quit;
        }

        ad_mbuf = _ip6_ndp_dad_na_gen(mbuf);
        if (LIKELY(ad_mbuf != NULL)) {
            tx->data[tx->count++] = ad_mbuf;
        }

        break;
    }

    case NDP_SOLICT_HAS_OPT_LEN: {
        uint16_t port = mbuf->port;
        struct pkt_tx *tx = &tlv_tx[port];
        const struct dpdk_ndp_opt *opt = NULL;

        if (UNLIKELY(!dpdk_ip6_addr_is_ucast(&ip6hdr->src_addr))) {
            goto _quit;
        }

        if (UNLIKELY(!dpdk_ip6_addr_is_ucast(&ndp->target))) {
            goto _quit;
        }

        opt = (const struct dpdk_ndp_opt *)(ndp + 1);
        if (UNLIKELY(opt->type != DPDK_NDP_SRC_LINK_OPT || opt->len != 1)) {
            goto _quit;
        }

        if (dpdk_ip6_addr_is_ucast(&ip6hdr->dst_addr)) {
            if (LIKELY(dpdk_ip6_addr_eq(&ip6hdr->dst_addr, &ndp->target))) {
                _ip6_ndp_table_update_and_notify(mbuf, &ip6hdr->src_addr, (const struct dpdk_mac *)opt->data, true, PKT_MBUF_NDP_SRC);
                ad_mbuf = _ip6_ndp_nud_na_gen(mbuf);
                if (LIKELY(ad_mbuf != NULL)) {
                    tx->data[tx->count++] = ad_mbuf;
                }
            }
        } else if (_ip6_target_is_solict_addr(&ip6hdr->dst_addr, &ndp->target)) {
            _ip6_ndp_table_update_and_notify(mbuf, &ip6hdr->src_addr, (const struct dpdk_mac *)opt->data, true, PKT_MBUF_NDP_SRC);
            ad_mbuf = _ip6_ndp_ns_mcast_na_ucast_gen(mbuf);
            if (LIKELY(ad_mbuf != NULL)) {
                tx->data[tx->count++] = ad_mbuf;
            }
        }

        break;
    }

    default: goto _quit;
    }

    // fallthrough

_quit:
    tlv_drop->data[tlv_drop->count++] = mbuf;
    return;
}

static INLINE void _ip6_ndp_advert_process(struct dpdk_mbuf *mbuf)
{
    uint8_t is_solict = 0;
    uint8_t is_override = 0;
    enum IP6_ADDR_TYPE addr_type;
    const struct dpdk_ndp_opt *opt = NULL;
    struct dpdk_ndp_hdr *ndp = DPDK_HEADROOM(mbuf)->l4;
    struct dpdk_ip6_hdr *ip6hdr = DPDK_HEADROOM(mbuf)->l3;

    if (UNLIKELY(!dpdk_icmp6_cksum_verify(mbuf, ip6hdr))) {
        goto _quit;
    }

    if (UNLIKELY(ip6hdr->hop_limits != UINT8_MAX)) {
        goto _quit;
    }

    if (UNLIKELY(ndp->icmp6_hdr.icmp6_code != 0)) {
        goto _quit;
    }

    if (UNLIKELY(ip6hdr->payload_len < NDP_AD_HAS_OPT_LEN)) {
        goto _quit;
    }

    opt = (const struct dpdk_ndp_opt *)(ndp + 1);
    if (UNLIKELY(opt->type != DPDK_NDP_DST_LINK_OPT || opt->len != 1)) {
        goto _quit;
    }

    if (UNLIKELY(!dpdk_ip6_addr_is_ucast(&ndp->target))) {
        goto _quit;
    }

    is_solict = ndp->icmp6_hdr.icmp6_dataun.u_nd_advt.solicited;
    if (UNLIKELY(is_solict && dpdk_ip6_addr_is_mcast(&ip6hdr->dst_addr))) {
        goto _quit;
    }

    addr_type = dpdk_ip6_addr_type(&ip6hdr->src_addr);
    switch (addr_type) {
    case IP6_ADDR_UNICAST:
        is_override = ndp->icmp6_hdr.icmp6_dataun.u_nd_advt.override;
        _ip6_ndp_table_update_and_notify(mbuf, &ndp->target, (struct dpdk_mac *)opt->data, is_override, PKT_MBUF_NDP_TARGET);
        break;
    case IP6_ADDR_UNSPEC: // DAD reponse
    default: goto _quit; break;
    }

    return;

_quit:
    tlv_drop->data[tlv_drop->count++] = mbuf;
    return;
}

static void _ip6_icmp_echo_request_process(struct dpdk_mbuf *mbuf)
{
    int ret = 0;
    int mtu = 0;
    struct pkt_tx *tx = NULL;

    ret = _ip6_icmp_reply(mbuf);
    if (UNLIKELY(ret != 0)) {
        tlv_drop->data[tlv_drop->count++] = mbuf;
        return;
    }

    mtu = tlv_dp->mtu;
    if (mbuf->pkt_len <= mtu) {
        tx = &tlv_tx[mbuf->port];
        tx->data[tx->count++] = mbuf;
    } else {
        _ip6_icmp_fragment(mbuf, mtu);
        tlv_drop->data[tlv_drop->count++] = mbuf;
    }
}

static void _ip6_icmp_process(struct dpdk_mbuf *data[], int count)
{
    struct dpdk_mbuf *mbuf;
    struct dpdk_icmp6_hdr *icmp6hdr;

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        icmp6hdr = DPDK_HEADROOM(mbuf)->l4;

        switch (icmp6hdr->icmp6_type) {
        case ICMPV6_ECHO_REQUEST:
            _ip6_icmp_echo_request_process(mbuf);
            break;
        case DPDK_NDP_SOLICT:
            _ip6_ndp_solict_process(mbuf);
            break;
        case DPDK_NDP_ADVERT:
            _ip6_ndp_advert_process(mbuf);
            break;
        default:
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        }
    }
}

static void _ip6_ndp_gen_bulk(void *data[], int count)
{
    int ret = 0;
    uint16_t port = 0;
    struct pkt_tx *tx = NULL;
    struct ndp_item *item = NULL;
    struct dpdk_mbuf *mbuf = NULL;

    ret = dpdk_pktmbuf_pop(tlv_dp->pktmbuf_pool, tlv_cache->data, count);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("pktmbuf no enough.");
        return;
    }

    for (int i = 0; i < count; i++) {
        item = data[i];
        port = item->port;

        mbuf = tlv_cache->data[i];
        if (UNLIKELY(!_ip6_ndp_nud_ns_gen(mbuf, port, &item->target, &item->mac))) {
            dpdk_pktmbuf_push(&tlv_cache->data[i], 1);
            continue;
        }

        tx = &tlv_tx[port];
        tx->data[tx->count++] = mbuf;
    }
}

void ip6_thread_ndp_table_destroy(void *nt)
{
    struct ndp_table *table = nt;

    if (table == NULL) {
        return;
    }

    if (table->pool != NULL) {
        dpdk_pool_destroy(table->pool);
    }

    for (int i = 0; i < table->nic_count; i++) {
        if (table->hash[i] != NULL) {
            dpdk_hash_destroy(table->hash[i]);
        }
    }

    dpdk_free(nt);
}

void *ip6_thread_ndp_table_create(int nic_count, int cpu_id, int hw_numa_id)
{
    char name[CACHE_LINE] = "";
    struct ndp_table *nt = NULL;

    nt = dpdk_malloc_numa(sizeof(struct ndp_table), hw_numa_id);
    if (UNLIKELY(nt == NULL)) {
        LOG_ERROR("OOM");
        return NULL;
    }

    memset(nt, 0, sizeof(struct ndp_table));

    snprintf(name, sizeof(name), "NDP_POOL_THREAD_%u_%u", hw_numa_id, cpu_id);
    nt->pool = dpdk_pool_ss_create(name, NDP_ITEM_MAX, sizeof(struct ndp_item), hw_numa_id);
    if (UNLIKELY(nt->pool == NULL)) {
        goto _quit;
    }

    nt->nic_count = nic_count;
    nt->ndp_count = 0;
    nt->timeout = NDP_TIMEOUT_DEFAULT;
    INIT_LIST_HEAD(&nt->man_head);
    INIT_LIST_HEAD(&nt->lru_head);

    for (int i = 0; i < nic_count; i++) {
        nt->hash[i] = dpdk_hash_create(NDP_ITEM_MAX, 16, hw_numa_id, dpdk_ip6_addr_cmp);
        if (UNLIKELY(nt->hash[i] == NULL)) {
            goto _quit;
        }
    }

    return nt;

_quit:
    ip6_thread_ndp_table_destroy(nt);
    return NULL;
}

void *ip6_startup(int nic_count, int hw_numa_id)
{
    int ret = 0;
    void *dst = NULL;

    ret = _ip6_conf_manage_create(&dst, nic_count, hw_numa_id);
    if (UNLIKELY(ret != 0)) {
        return NULL;
    }

    return dst;
}

void ip6_process(void *data[], int count)
{
    int icmp6_count = 0;
    uint64_t *result = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;

    result = (uint64_t *)tlv_cache->data;
    _ip6_is_local_bulk(data, result, count);

    for (int i = 0; i < count; i++) {
        mbuf = data[i];

        ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);
        if (UNLIKELY(!dpdk_bit_test_u64(result[i / 64], i & 63) && !dpdk_ip6_addr_is_mcast(&ip6hdr->dst_addr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY(!dpdk_ip6_version_check(ip6hdr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY(dpdk_pktmbuf_ip6_frag_hdr(ip6hdr) != NULL)) {
            if (UNLIKELY(dpdk_be_to_cpu_16(ip6hdr->payload_len) <= sizeof(struct dpdk_ip6_frag_ext))) {
                tlv_drop->data[tlv_drop->count++] = mbuf;
                continue;
            }

            mbuf->l2_len = sizeof(struct dpdk_eth_hdr);
            mbuf->l3_len = sizeof(struct dpdk_ip6_hdr) + sizeof(struct dpdk_ip6_frag_ext);
            mbuf = dpdk_ip6_mbuf_reassemble(tlv_dp->frag_handle, mbuf, tlv_dp->timer_cycles, ip6hdr, ip6hdr + 1);

            if (mbuf != NULL) {
                dpdk_ip_reassemble_finish(tlv_dp->frag_handle, mbuf->nb_segs);
                ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);
            } else {
                dpdk_ip_reassemble_pending(tlv_dp->frag_handle, tlv_dp->timer_cycles);
                continue;
            }
        }

        /*
         * Currently, only the Fragment Header extension is supported;
         * all other IPv6 extension headers are not supported.
         */
        switch (ip6hdr->proto) {
        case IPPROTO_TCP:
            DPDK_HEADROOM(mbuf)->l3 = ip6hdr;
            DPDK_HEADROOM(mbuf)->l4 = (ip6hdr + 1);
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        case IPPROTO_ICMPV6:
            DPDK_HEADROOM(mbuf)->l3 = ip6hdr;
            DPDK_HEADROOM(mbuf)->l4 = (ip6hdr + 1);
            tlv_icmp6->data[tlv_icmp6->count++] = mbuf;
            break;
        default:
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        }
    }

    icmp6_count = tlv_icmp6->count;
    if (icmp6_count != 0) {
        _ip6_icmp_process((struct dpdk_mbuf **)tlv_icmp6->data, icmp6_count);
        tlv_icmp6->count = 0;
    }
}

void ip6_destroy(void *ptr)
{
    _ip6_conf_manage_destroy(ptr);
}

void ip6_ndp_refresh(void)
{
    int count = 0;
    int pending_count = 0;
    struct ndp_item *cur = NULL;
    struct ndp_item *next = NULL;
    uint32_t cur_time = tlv_dp->off_time;
    struct ndp_table *nt = ((struct proto_header *)tlv_dp->protocol)->nt;

    static __thread void *s_ndp_gc[NDP_GC_MAX] = {NULL};

    list_for_each_entry_safe_reverse(cur, next, &nt->lru_head, lru_node) {
        if (cur->expire_time > cur_time) {
            if (NDP_TIME_TO_SEND(cur, cur_time)) {
                cur->retry += 1;
                cur->send_time = cur_time;
                tlv_pending->data[tlv_pending->count++] = cur;
            } else {
                break;
            }
        } else {
            list_del_init(&cur->lru_node);
            list_del_init(&cur->node);
            nt->ndp_count -= 1;
            s_ndp_gc[count++] = cur;
            if (UNLIKELY(count == NDP_GC_MAX)) {
                break;
            }
        }
    }

    pending_count = tlv_pending->count;
    if (pending_count != 0) {
        _ip6_ndp_gen_bulk(tlv_pending->data, pending_count);
        tlv_pending->count = 0;
    }

    if (count != 0) {
        dpdk_mempool_push(nt->pool, s_ndp_gc, count);
    }
}

void ip6_ndp_update_or_create(void *data)
{
    struct dpdk_mbuf *mbuf = data;

    struct dpdk_ip6_addr *addr = NULL;
    struct dpdk_ip6_hdr *ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);
    struct dpdk_ndp_hdr *ndphdr = dpdk_pktmbuf_ndp_hdr(mbuf);
    struct dpdk_ndp_opt *ndpopt = (struct dpdk_ndp_opt *)(ndphdr + 1);
    int override = ndphdr->icmp6_hdr.icmp6_dataun.u_nd_advt.override;

    switch (DPDK_HEADROOM(mbuf)->type) {
    case PKT_MBUF_NDP_SRC:
        addr = &ip6hdr->src_addr;
        break;
    case PKT_MBUF_NDP_TARGET:
        addr = &ndphdr->target;
        break;
    default: return;
    }

    _ip6_ndp_table_update(mbuf->port, addr, (struct dpdk_mac *)ndpopt->data, override);
}