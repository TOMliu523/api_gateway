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
#include "type.h"
#include "macro.h"
#include "errcode.h"
#include "dpdk_rcu.h"
#include "dpdk_hash.h"
#include "dpdk_core.h"
#include "dpdk_fib6.h"
#include "dpdk_icmp6.h"

#define L3_IPv6_BUCKET_MAX (1 << 16)
#define L3_IPv6_ONLY_VERSION 0x60000000

#define L3_ETH_AND_IPv6_LEN (sizeof(struct dpdk_eth) + sizeof(struct dpdk_ip6_hdr))

struct ip6_manage {
    int ip_count;
    int nic_count;
    struct dpdk_fib6 *fib[DPDK_ETHPORT_MAX];
    struct dpdk_hash *hash[DPDK_ETHPORT_MAX];
    struct ip6_info *master[DPDK_ETHPORT_MAX];
    struct ip6_info store[L3_IPv6_BUCKET_MAX];
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

static int _ip6_conf_info_cmp(const void *key1, const void *key2, size_t len)
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

static int _ip6_conf_manage_del_check(struct ip6_manage *manage, const struct ip6_info *info, int count)
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
        inet_ntop(AF_INET6, &info->addr, ip_str, sizeof(ip_str));
        LOG_ERROR("IP: %s, port: %d not exists", ip_str, info->port);
        return ERRCODE_IP_NOT_EXIST;
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

    if (UNLIKELY(manage->ip_count + count > L3_IPv6_BUCKET_MAX)) {
        LOG_ERROR("Maximum supported IP address count(%d) exceeded", L3_IPv6_BUCKET_MAX);
        return ERRCODE_IP_LIMIT_EXCEEDED;
    }

    for (int i = 0; i < count; i++) {
        one = &info[i];

        ret = dpdk_fib6_lookup(manage->fib[one->port], &info->addr, &next_hop, 1);
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
        manage->hash[i] = dpdk_hash_create(L3_IPv6_BUCKET_MAX, sizeof(struct dpdk_ip6_addr), hw_numa_id, _ip6_conf_info_cmp);
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
    _ip6_conf_manage_destroy(manage);
    return ERRCODE_OOM;
}

static int _ip6_conf_manage_append(struct ip6_manage *dst, const struct ip6_manage *src, const struct ip6_info *info, int count)
{
    int ret = 0;
    int nums = 0;

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
    int nums = 0;
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
    const struct ip6_info *cur = NULL;
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

int ip6_ndp_advertisement_gen(struct dpdk_mbuf *mbuf, uint16_t port, const struct dpdk_ip6_addr *addr, const struct dpdk_mac *mac)
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
    ip6hdr->src_addr = *addr;
    ip6hdr->dst_addr = s_ipv6_multicast_ip;

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

static INLINE int _ip6_is_local_bulk(void *data[], uint64_t result[], int count)
{
    int n = 0;
    bool hit = false;
    struct ip6_info *cur = NULL;
    struct list_head *head = NULL;
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

    mbuf = data[0];
    return dpdk_hash_lookup_bulk(manage->hash[mbuf->port], (const void **)keys, count, result, outs);
}

static INLINE void _ip6_icmp_fragment(struct dpdk_mbuf *mbuf, uint16_t mtu)
{
    int rc = 0;
    int len = 0;
    int tx_count = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_eth *eth = NULL;
    struct dpdk_mbuf *pkt = NULL;
    struct dpdk_eth *src_eth = dpdk_pktmbuf_eth(mbuf);
    struct dpdk_mac src_addr = src_eth->src_addr;
    struct dpdk_mac dst_addr = src_eth->dst_addr;

    dpdk_pktmbuf_adj(mbuf, sizeof(struct dpdk_eth));

    tx = &tlv_tx[mbuf->port];
    tx_count = tx->count;
    rc = dpdk_ip6_mbuf_fragment(mbuf, &tx->data[tx->count], 64, mtu, tlv_dp->pktmbuf_pool, tlv_dp->indirect_pool);
    if (UNLIKELY(rc < 0)) {
        return;
    }

    len = tx_count + rc;
    for (int i = tx_count; i < len; i++) {
        pkt = tx->data[i];

        pkt->l2_len = sizeof(struct dpdk_eth);
        pkt->l3_len = sizeof(struct dpdk_ip6_hdr) + sizeof(struct dpdk_ip6_frag_ext);

        eth = dpdk_pktmbuf_prepend(pkt, pkt->l2_len);
        eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_TYPE_IP6);
        eth->dst_addr = dst_addr;
        eth->src_addr = src_addr;
    }

    tx->count = len;
}

static INLINE void _ip6_icmp_echo_request_process(struct dpdk_mbuf *mbuf)
{
    struct dpdk_eth *eth = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;
    struct dpdk_icmp6_hdr *icmp6hdr = NULL;

    icmp6hdr = DPDK_HEADROOM(mbuf)->l4;
    if (UNLIKELY(icmp6hdr->icmp6_code != 0)) {
        tlv_drop->data[tlv_drop->count++] = mbuf;
        return;
    }

    ip6hdr = DPDK_HEADROOM(mbuf)->l3;
    if (UNLIKELY(dpdk_icmp6_cksum_verify(mbuf, ip6hdr))) {
        tlv_drop->data[tlv_drop->count++] = mbuf;
        return;
    }

    eth = DPDK_HEADROOM(mbuf)->l2;
    SWAP(eth->src_addr, eth->dst_addr);

    SWAP(ip6hdr->src_addr, ip6hdr->dst_addr);

    dpdk_icmp6_echo_reply_ckcum(icmp6hdr);
}

static INLINE bool _ip6_ndp_solict_process(struct dpdk_mbuf *mbuf)
{
    struct dpdk_ip6_hdr *ip6hdr = DPDK_HEADROOM(mbuf)->l3;
    struct dpdk_icmp6_hdr *icmp6hdr = DPDK_HEADROOM(mbuf)->l4;

    if (UNLIKELY(ip6hdr->hop_limits != UINT8_MAX)) {
        return false;
    }


}

static INLINE bool _ip6_ndp_advert_process(struct dpdk_mbuf *mbuf)
{

}

static void _ip6_icmp_process(struct dpdk_mbuf *data[], int count)
{
    uint16_t mtu;
    struct dpdk_mbuf *mbuf;
    struct pkt_tx *tx = NULL;
    struct dpdk_icmp6_hdr *icmp6hdr;

    mtu = tlv_dp->mtu;
    tx = &tlv_tx[mbuf->port];
    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        icmp6hdr = DPDK_HEADROOM(mbuf)->l4;

        switch (icmp6hdr->icmp6_type) {
        case ICMPV6_ECHO_REQUEST:
            _ip6_icmp_echo_request_process(mbuf);
            if (mbuf->pkt_len <= mtu) {
                tx->data[tx->count++] = mbuf;
            } else {
                _ip6_icmp_fragment(mbuf, mtu);
                tlv_drop->data[tlv_drop->count++] = mbuf;
            }
            break;
        case DPDK_NDP_SOLICT:
            if (_ip6_ndp_solict_process(mbuf)) {
                tx->data[tx->count++] = mbuf;
            } else {
                tlv_drop->data[tlv_drop->count++] = mbuf;
            }
            break;
        case DPDK_NDP_ADVERT:
            if (_ip6_ndp_advert_process(mbuf)) {
                tx->data[tx->count++] = mbuf;
            } else {
                tlv_drop->data[tlv_drop->count++] = mbuf;
            }
            break;
        default:
            tlv_drop->data[tlv_drop->count++] = mbuf;
            break;
        }
    }
}

void ip6_process(void *data[], int count)
{
    uint64_t *result = 0;
    int ip_check_count = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_ip6_hdr *ip6hdr = NULL;

    result = (uint64_t *)tlv_cache->data;
    _ip6_is_local_bulk(data, result, count);

    for (int i = 0; i < count; i++) {
        mbuf = data[i];

        ip6hdr = dpdk_pktmbuf_ip6_hdr(mbuf);
        if (UNLIKELY((result[i >> 6] & (i & 63)) == 0 && !dpdk_ip6_addr_is_mcast(&ip6hdr->dst_addr))) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY(ip6hdr->version != 6)) {
            tlv_drop->data[tlv_drop->count++] = mbuf;
            continue;
        }

        if (UNLIKELY(dpdk_pktmbuf_ip6_frag_hdr(ip6hdr) != NULL)) {
            if (UNLIKELY(ip6hdr->payload_len <= sizeof(struct dpdk_ip6_frag_ext))) {
                tlv_drop->data[tlv_drop->count++] = mbuf;
                continue;
            }

            mbuf->l2_len = sizeof(struct dpdk_eth);
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
        case IPPROTO_UDP:
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

    if (tlv_icmp6->count != 0) {
        _ip6_icmp_process((struct dpdk_mbuf **)tlv_icmp6->data, tlv_icmp->count);
        tlv_icmp6->count = 0;
    }
}

void ip6_manage_destroy(void *ptr)
{
    _ip6_conf_manage_destroy(ptr);
}