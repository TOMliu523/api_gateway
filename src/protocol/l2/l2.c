 /*****************************************************************************
 * filename: l2.c
 * function:
 * description:
 *****************************************************************************/

#include <stdint.h>
#include <string.h>

#include "l2.h"
#include "ip4.h"
#include "log.h"
#include "type.h"
#include "list.h"
#include "dpdk_ip4.h"
#include "dpdk_ip6.h"
#include "protocol.h"
#include "dpdk_port.h"
#include "dpdk_core.h"
#include "dpdk_common.h"

#if defined(__ORDER_LITTLE_ENDIAN__)
#define L2_ARP_REQUEST (0x0100040600080100UL)
#define L2_ARP_RESPONSE (0x0200040600080100UL)
#else
#define L2_ARP_REQUEST (0x0001080006040001UL)
#define L2_ARP_RESPONSE (0x0001080006040002UL)
#endif // __ORDER_LITTLE_ENDIAN__

#define ARP_GC_MAX 64
#define ARP_ITEM_MAX 65536
#define ARP_SEND_REQUEST_JUDGE(c, t) (((c)->expire_time - (5 - (c)->retry) * 60) > (t))

#define ARP_TIMEOUT_DEFAULT (20 * 60)
#define ARP_HASH_BUCKET_MAX (65536)
#define ARP_HASH_BUCKET(x) ((x) >> 16)

enum ARP_HWTYPE {
    ARP_HW_TYPE_ETHER = 1,
};

// ARP entry status flags
enum ARP_FLAGS {
    ARP_FLAGS_MANUAL,
    ARP_FLAGS_COMPLETE,
    ARP_FLAGS_INCOMPLETE,
};

// ARP entry structure (aligned to cache line)
struct arp_item {
    union {
        struct {
            // Main list node for per-port hash bucket
            struct list_head node;
            union {
                // List node for manual/static entries
                struct list_head man_node;
                // List node for LRU-managed dynamic entries
                struct list_head lru_node;
                // List node for incomplete/probing entries
                struct list_head incomplete_node;
            };
            // Destination IP address (network byte order / big-endian)
            uint32_t ip;
            // Resolved MAC address (valid when flags == COMPLETE)
            struct dpdk_mac mac;
            // Expiration timestamp (for aging)
            uint32_t expire_time;
            // DPDK port ID (0 ~ DPDK_ETHPORT_MAX)
            uint16_t port;
            // Hardware type (e.g., Ethernet = 1)
            uint8_t hwtype;
            // Status flag (from ARP_FLAGS)
            uint8_t flags;
            // Retry counter (for incomplete/probing entries)
            uint8_t retry;
            // Last probe/request send timestamp
            uint32_t send_time;
        };
        // Explicit cache-line alignment padding
        uint8_t cache_line[CACHE_LINE];
    };
} ALIGN_CACHE_LINE;

// ARP table structure
struct arp_table {
    // Pointer to the memory pool used for allocating arp_item
    // Assumed to be single-writer, single-reader (SWSR) safe
    void *pool;
    // Number of network interfaces (ports)
    int nic_count;
    // Total number of active ARP entries
    int arp_count;
    // ARP entry timeout in seconds (used for aging)
    uint32_t timeout;
    struct {
        // Head of manually configured (static) entries
        struct list_head man_head;
        // Head of dynamically learned entries (LRU)
        struct list_head lru_head;
        // Heads for incomplete entries under probing
        struct list_head incomplete_head[ARP_HASH_BUCKET_MAX];
    };
    // Hash table mapping [port][bucket] → list of arp_item
    // Used for fast ARP entry lookup by IP address per port
    struct list_head hash[DPDK_ETHPORT_MAX][ARP_HASH_BUCKET_MAX];
};

static __thread int s_arp_cache_count = 0;
static __thread void *s_arp_cache[64] = {NULL};
static __thread struct dpdk_mac *s_mac;
static __thread struct arp_table *s_arp_table;
static __thread struct dpdk_mac s_broadcast = {
    .addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};

int l2_conf_port_mac(uint16_t port, struct dpdk_mac *mac)
{
    int ret = 0;

    ret = dpdk_port_mac(port, mac);
    if (ret != 0) {
        LOG_ERROR("Failure port(%d) dpdk_port_mac: %s", port, strerror(-ret));
        return -1;
    }

    return ret;
}

static INLINE void _l2_arp_part_init(struct arp_item *item, struct dpdk_mac *mac, uint32_t expire_time)
{
    item->mac = *mac;
    item->expire_time = expire_time;
    item->flags = ARP_FLAGS_COMPLETE;
    item->retry = 0;
    item->send_time = 0;
}

static INLINE void _l2_arp_init(struct arp_item *item, uint32_t ip, struct dpdk_mac *mac, uint16_t port, uint32_t expire_time)
{
    INIT_LIST_HEAD(&item->node);
    INIT_LIST_HEAD(&item->lru_node);
    item->ip = ip;
    item->mac = *mac;
    item->expire_time = expire_time;
    item->port = port;
    item->hwtype = ARP_HW_TYPE_ETHER;
    item->flags = ARP_FLAGS_COMPLETE;
    item->retry = 0;
    item->send_time = 0;
}

static INLINE void _l2_arp_reply(struct dpdk_mbuf *mbuf, uint16_t port)
{
    uint32_t dst_ip = 0;
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_arp_hdr *arp = NULL;
    struct dpdk_mac *src_mac = NULL;

    eth = dpdk_pktmbuf_eth(mbuf);
    arp = dpdk_pktmbuf_arp(mbuf);

    src_mac = &s_mac[port];

    eth->dst_addr = eth->src_addr;
    eth->src_addr = *src_mac;

    *(uint64_t *)arp = L2_ARP_RESPONSE;

    // arp_data
    dst_ip = arp->arp_data.arp_tip;
    arp->arp_data.arp_tha = arp->arp_data.arp_sha;
    arp->arp_data.arp_tip = arp->arp_data.arp_sip;
    arp->arp_data.arp_sip = dst_ip;
    arp->arp_data.arp_sha = *src_mac;
}

static INLINE void _l2_arp_probe_reply(struct dpdk_mbuf *mbuf, uint16_t port)
{
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_arp_hdr *arp = NULL;
    struct dpdk_mac *mac = NULL;
    struct dpdk_arp_hdr_data *arp_data = NULL;

    eth = dpdk_pktmbuf_eth(mbuf);
    arp = dpdk_pktmbuf_arp(mbuf);

    mac = &s_mac[port];

    eth->dst_addr = eth->src_addr;
    eth->src_addr = *mac;

    *(uint64_t *)arp = L2_ARP_RESPONSE;

    arp_data = &arp->arp_data;
    arp_data->arp_tha = arp_data->arp_sha;
    arp_data->arp_sha = *mac;
    arp_data->arp_sip = arp_data->arp_tip;
    arp_data->arp_tip = arp_data->arp_sip;
}

static INLINE int __l2_arp_gen(struct dpdk_mbuf *mbuf, uint16_t port, uint32_t src_ip, uint32_t target_ip)
{
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_arp_hdr *arp = NULL;
    struct dpdk_mac *mac = NULL;

    eth = dpdk_append(mbuf, sizeof(*eth) + sizeof(*arp), struct dpdk_eth_hdr *);
    if (UNLIKELY(eth == NULL)) {
        LOG_ERROR("There is not enough tailroom space in the last segment.");
        return -1;
    }

    mbuf->port = port;
    mac = &s_mac[port];

    eth->dst_addr = s_broadcast;
    eth->src_addr = *mac;
    eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_ARP);

    arp = (struct dpdk_arp_hdr *)(eth + 1);
    *(uint64_t *)&arp->arp_hardware = L2_ARP_REQUEST;

    // arp_data
    arp->arp_data.arp_sha = *mac;
    arp->arp_data.arp_sip = src_ip;
    arp->arp_data.arp_tha = s_broadcast;
    arp->arp_data.arp_tip = target_ip;

    return 0;
}

static void _arp_update_or_create(struct arp_table *at, uint16_t port, uint32_t ip, struct dpdk_mac *mac, uint32_t t)
{
    int ret = 0;
    struct arp_item *item = NULL;
    struct arp_item *cur_item = NULL;
    struct arp_item *next_item = NULL;

    int idx = ARP_HASH_BUCKET(ip);
    struct list_head *lru_head = &at->lru_head;
    struct list_head *head = &at->hash[port][idx];

    list_for_each_entry_safe(cur_item, next_item, head, node) {
        if (cur_item->ip == ip) {
            switch (cur_item->flags) {
            case ARP_FLAGS_COMPLETE:
                _l2_arp_part_init(cur_item, mac, t + at->timeout);
                list_del_init(&cur_item->lru_node);
                list_add(&cur_item->lru_node, lru_head);
                return;

            case ARP_FLAGS_INCOMPLETE:
                _l2_arp_part_init(cur_item, mac, t + at->timeout);
                list_del_init(&cur_item->incomplete_node);
                list_add(&cur_item->incomplete_node, lru_head);
                return;

            default:
                LOG_ERROR("Manual ARP entries cannot be updated.");
                break;
            }
        } else if (cur_item->ip < ip) {
            continue;
        } else {
            break;
        }
    }

    if (s_arp_cache_count == 0) {
        ret = dpdk_mempool_pop(at->pool, s_arp_cache, ARR_NUMS(s_arp_cache));
        if (LIKELY(ret == 0)) {
            s_arp_cache_count = ARR_NUMS(s_arp_cache);
        } else {
            ret = dpdk_mempool_pop(at->pool, s_arp_cache, 1);
            if (LIKELY(ret == 0)) {
                s_arp_cache_count = 1;
            }
        }

        if (UNLIKELY(s_arp_cache_count == 0)) {
            LOG_ERROR("The ARP table has reached its limit; no new entries can be added.");
            return;
        }
    }

    item = s_arp_cache[--s_arp_cache_count];

    _l2_arp_init(item, ip, mac, port, at->timeout + t);
    list_add_tail(&item->node, &cur_item->node);
    list_add(&item->lru_node, lru_head);

    at->arp_count += 1;
}

static INLINE void _l2_arp_gen(int (*func)(uint32_t *, uint32_t, int))
{
    int ret = 0;
    int count = 0;
    uint32_t src_ip = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_mbuf *mbuf = NULL;

    count = dpdk_pktmbuf_pop(tlv_dp->pktmbuf_pool, tlv_cache->data, tlv_pending->count);
    if (UNLIKELY(count != 0)) {
        LOG_ERROR("pktmbuf no enough.");
        return;
    }

    count = tlv_pending->count;
    for (int i = 0; i < count; i++) {
        uint16_t port = 0;
        struct arp_item *item = tlv_pending->data[i];

        port = item->port;
        tx = &tlv_tx[port];

        ret = func(&src_ip, item->ip, port);
        if (UNLIKELY(ret != 0)) {
            continue;
        }

        mbuf = tlv_cache->data[i];
        ret = __l2_arp_gen(mbuf, item->port, src_ip, item->ip);
        if (UNLIKELY(ret != 0)) {
            dpdk_pktmbuf_push(tlv_cache->data, count);
            return;
        }

        tx->data[tx->count++] = mbuf;
    }

    return;
}

void l2_arp_refresh(int (*func)(uint32_t *, uint32_t, int))
{
    int count = 0;
    struct arp_item *cur = NULL;
    struct arp_item *next = NULL;
    uint32_t time = tlv_dp->off_time;
    struct arp_table *table = ((struct proto_header *)tlv_dp->protocol)->at;

    static __thread void *s_arp_gc[ARP_GC_MAX] = {NULL};

    list_for_each_entry_safe_reverse(cur, next, &table->lru_head, lru_node) {
        if (cur->expire_time > time) {
            if (ARP_SEND_REQUEST_JUDGE(cur, time)) {
                break;
            } else {
                cur->retry += 1;
                cur->send_time = time;
                tlv_pending->data[tlv_pending->count++] = cur;
            }
        } else { // expired
            list_del_init(&cur->lru_node);
            list_del_init(&cur->node);
            table->arp_count -= 1;
            s_arp_gc[count++] = cur;
            if (UNLIKELY(count == ARP_GC_MAX)) {
                break;
            }
        }
    }

    if (tlv_pending->count != 0) {
        _l2_arp_gen(func);
        tlv_pending->count = 0;
    }

    if (count != 0) {
        dpdk_mempool_push(table->pool, s_arp_gc, count);
    }
}

static INLINE void _l2_add_arp_notify(void *data[], int count, struct dpdk_mbuf *mbuf)
{
    DPDK_HEADROOM(mbuf)->type = PKT_MBUF_ARP;
    data[count] = mbuf;
}

static void _l2_arp_parse(void *data[], int count)
{
    uint16_t port = 0;
    struct pkt_tx *tx = NULL;
    struct dpdk_arp_hdr *arp = NULL;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_arp_hdr_data *arp_data = NULL;
    struct proto_header *proto = tlv_dp->protocol;
    struct arp_table *at = proto->at;

    int drop_count = tlv_drop->count;
    int notify_count = tlv_notify->count;

    mbuf = data[0];
    port = mbuf->port;
    tx = &tlv_tx[mbuf->port];

    for (int i = 0; i < count; i++) {
        mbuf = data[i];
        arp = dpdk_pktmbuf_arp(mbuf);
        arp_data = &arp->arp_data;

        switch (*(uint64_t *)arp) {
        case L2_ARP_RESPONSE:
            switch (ip4_local_class(port, arp_data->arp_tip)) {
            case IP_LOCAL_CLASS_SELF:
            case IP_LOCAL_CLASS_LAN:
                _arp_update_or_create(at, port, arp_data->arp_sip, &arp_data->arp_sha, tlv_dp->off_time);
                _l2_add_arp_notify(tlv_notify->data, notify_count++, mbuf);
                break;
            default:
                tlv_drop->data[drop_count++] = mbuf;
                break;
            }
            break;

        case L2_ARP_REQUEST:
            if (arp_data->arp_sip != arp_data->arp_tip && arp_data->arp_sip != 0) { // ARP Response
                switch (ip4_local_class(port, arp_data->arp_tip)) {
                case IP_LOCAL_CLASS_SELF:
                    _arp_update_or_create(at, port, arp_data->arp_sip, &arp_data->arp_sha, tlv_dp->off_time);
                    _l2_add_arp_notify(tlv_notify->data, notify_count++, mbuf);

                    mbuf = dpdk_pktmbuf_copy(mbuf, tlv_dp->pktmbuf_pool);
                    if (UNLIKELY(mbuf == NULL)) {
                        break;
                    }

                    _l2_arp_reply(mbuf, port);
                    tx->data[tx->count++] = mbuf;
                    break;
                case IP_LOCAL_CLASS_LAN:
                    _arp_update_or_create(at, port, arp_data->arp_sip, &arp_data->arp_sha, tlv_dp->off_time);
                    _l2_add_arp_notify(tlv_notify->data, notify_count++, mbuf);
                    break;
                default:
                    tlv_drop->data[drop_count++] = mbuf;
                    break;
                }
            } else if (arp_data->arp_sip == 0) {
                switch (ip4_local_class(port, arp_data->arp_tip)) {
                case IP_LOCAL_CLASS_SELF:
                    _l2_arp_probe_reply(mbuf, port);
                    tx->data[tx->count++] = mbuf;
                    break;
                default:
                    tlv_drop->data[drop_count++] = mbuf;
                    break;
                }
            } else {
                switch (ip4_local_class(port, arp_data->arp_tip)) {
                case IP_LOCAL_CLASS_SELF:
                    LOG_WARN("Local IP address is already in use.");
                    tlv_drop->data[drop_count++] = mbuf;
                    break;
                case IP_LOCAL_CLASS_LAN:
                    _arp_update_or_create(at, port, arp_data->arp_sip, &arp_data->arp_sha, tlv_dp->off_time);
                    _l2_add_arp_notify(tlv_notify->data, notify_count++, mbuf);
                    break;
                default:
                    tlv_drop->data[drop_count++] = mbuf;
                    break;
                }
            }
            break;

        default:
            tlv_drop->data[drop_count++] = mbuf;
            break;
        }
    }

    tlv_drop->count = drop_count;
    tlv_notify->count = notify_count;
}

void l2_arp_update_or_create(void *arg)
{
    struct dpdk_mbuf *mbuf = arg;
    struct proto_header *proto = tlv_dp->protocol;
    struct arp_table *at = proto->at;
    struct dpdk_arp_hdr *arp = dpdk_pktmbuf_arp(mbuf);
    struct dpdk_arp_hdr_data *arp_data = &arp->arp_data;

    _arp_update_or_create(at, mbuf->port, arp_data->arp_sip, &arp_data->arp_sha, tlv_dp->off_time);
}

void *l2_thread_arp_table_create(int nic_count, int cpu_id, int numa_id)
{
    size_t len = 0;
    struct arp_table *table = NULL;
    char name[BUFSIZ] = "";

    len = sizeof(*table) + nic_count * (sizeof(table->hash) + sizeof(**table->hash));
    table = dpdk_malloc(len);
    if (table == NULL) {
        LOG_ERROR("OOM.");
        return NULL;
    }

    memset(table, 0, len);

    snprintf(name, sizeof(name), "ARP_POOL_THREAD_%02d%03d", numa_id, cpu_id);
    table->pool = dpdk_pool_ss_create(name, ARP_ITEM_MAX, sizeof(struct arp_item), numa_id);
    if (table->pool == NULL) {
        goto _quit;
    }

    table->arp_count = 0;
    table->nic_count = nic_count;
    table->timeout = ARP_TIMEOUT_DEFAULT;
    INIT_LIST_HEAD(&table->man_head);
    INIT_LIST_HEAD(&table->lru_head);

    for (int i = 0; i < ARR_NUMS(table->incomplete_head); i++) {
        INIT_LIST_HEAD(&table->incomplete_head[i]);
    }

    for (int i = 0; i < DPDK_ETHPORT_MAX; i++) {
        for (int j = 0; j < ARP_HASH_BUCKET_MAX; j++) {
            INIT_LIST_HEAD(&table->hash[i][j]);
        }
    }

    s_arp_table = table;
    return table;

_quit:
    l2_thread_arp_table_destroy(table);
    return NULL;
}

void l2_thread_arp_table_destroy(void *table)
{
    if (table == NULL) {
        return;
    }

    dpdk_pool_destroy(((struct arp_table *)table)->pool);
    dpdk_free(table);
}

int l2_mac_get(struct dpdk_mac *mac, int port, uint32_t be_ip)
{
    struct arp_item *item = NULL;
    struct list_head *head = NULL;
    struct arp_table *table = s_arp_table;

    if (UNLIKELY(port >= table->nic_count)) {
        LOG_ERROR("port error(cur : %d, max: %d)", port, table->nic_count);
        return -1;
    }

    head = &table->hash[port][ARP_HASH_BUCKET(be_ip)];
    list_for_each_entry(item, head, node) {
        if (item->ip == be_ip) {
            *mac = item->mac;
            return 0;
        } else if (item->ip < be_ip) {
            continue;
        } else {
            break;
        }
    }

    return -1;
}

int l2_gratuitous_arp_gen(struct dpdk_mbuf *mbuf, uint16_t port, uint32_t addr, struct dpdk_mac *mac)
{
    struct dpdk_eth_hdr *eth = NULL;
    struct dpdk_arp_hdr *arp = NULL;

    eth = dpdk_append(mbuf, sizeof(*eth) + sizeof(*arp), struct dpdk_eth_hdr *);
    if (UNLIKELY(eth == NULL)) {
        LOG_ERROR("There is not enough tailroom space in the last segment.");
        return -1;
    }

    mbuf->port = port;

    eth->dst_addr = s_broadcast;
    eth->src_addr = *mac;
    eth->ether_type = dpdk_cpu_to_be_16(DPDK_ETHER_ARP);

    arp = (struct dpdk_arp_hdr *)(eth + 1);
    *(uint64_t *)arp = L2_ARP_REQUEST;

    // arp_data
    arp->arp_data.arp_sha = *mac;
    arp->arp_data.arp_sip = addr;
    arp->arp_data.arp_tha = s_broadcast;
    arp->arp_data.arp_tip = addr;

    return 0;
}

void l2_process(void *data[], int count)
{
    int arp_count = 0;
    struct dpdk_mbuf *mbuf = NULL;
    struct dpdk_eth_hdr *eth = NULL;

    UNROLL_LOOP_8(i, count, {
        if (i + 1 < count) {
            dpdk_prefetch0(data[i + 1]);

            void *ptr = dpdk_pktmbuf_eth(data[i + 1]);
            dpdk_prefetch0(ptr);
        }
    });

    /*
     * Letting the compiler optimize this loop usually yields better performance,
     * unless manually optimized with SIMD for comparison efficiency.
     */
    // for (int i = 0; i < count; i++) {
    UNROLL_LOOP_8(i, count, {
        mbuf = data[i];
        eth = dpdk_pktmbuf_eth(mbuf);

        if (MAC_ADDR_CMP(&eth->dst_addr, &s_mac[mbuf->port]) || MAC_IS_TO_LOCAL(&eth->dst_addr)) {
            switch (dpdk_be_to_cpu_16(eth->ether_type)) {
            case DPDK_ETHER_ARP:
                /*
                 * Because each received packet has a minimum length of 60 bytes,
                 * we skip length checks when it is smaller than 60 bytes and only
                 * perform a length check once at Layer 4.
                 * Scatter, buffer split, and header split must be disabled
                 */
                tlv_arp->data[tlv_arp->count++] = mbuf;
                break;
            case DPDK_ETHER_IP4:
                DPDK_HEADROOM(mbuf)->l2 = eth;
                tlv_ip4->data[tlv_ip4->count++] = mbuf;
                break;
            case DPDK_ETHER_IP6:
                DPDK_HEADROOM(mbuf)->l2 = eth;
                tlv_ip6->data[tlv_ip6->count++] = mbuf;
                break;
            default:
                tlv_drop->data[tlv_drop->count++] = mbuf;
                break;
            }
        } else {
            tlv_drop->data[tlv_drop->count++] = mbuf;
        }
    });

    arp_count = tlv_arp->count;
    if (arp_count != 0) {
        _l2_arp_parse(tlv_arp->data, arp_count);
        tlv_arp->count = 0;
    }
}

void *l2_thread_mac_create(int nic_count)
{
    struct dpdk_mac *mac = NULL;

    mac = dpdk_malloc(nic_count * sizeof(*mac));
    if (UNLIKELY(mac == NULL)) {
        LOG_ERROR("OOM");
        return NULL;
    }

    for (int i = 0; i < nic_count; i++) {
        dpdk_port_mac(i, &mac[i]);
    }

    s_mac = mac;
    return mac;
}

void l2_thread_mac_destroy(void *ptr)
{
    if (UNLIKELY(ptr == NULL)) {
        return;
    }

    dpdk_free(ptr);
}

void l2_thread_port_mac(uint16_t port, struct dpdk_mac *mac)
{
    *mac = s_mac[port];
}