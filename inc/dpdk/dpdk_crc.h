/*****************************************************************************
 * filename: dpdk_crc.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __DPDK_CRC_H__
#define __DPDK_CRC_H__

#include <rte_hash_crc.h>

#include "macro.h"

static INLINE void dpdk_hash_crc_set_alg(void)
{
    rte_hash_crc_set_alg(CRC32_SSE42_x64);
}

static INLINE uint32_t dpdk_hash_crc(const void *data, uint32_t data_len)
{
    return rte_hash_crc(data, data_len, 0);
}

static INLINE uint32_t dpdk_hash_crc_with_init_val(const void *data, uint32_t data_len, uint32_t init_val)
{
    return rte_hash_crc(data, data_len, init_val);
}

static INLINE uint32_t dpdk_hash_crc_add(uint32_t init_val, const void *data, uint32_t data_len)
{
    return rte_hash_crc(data, data_len, init_val);
}

#endif // __DPDK_CRC_H__