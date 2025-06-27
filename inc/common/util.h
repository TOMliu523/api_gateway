/************************************************
 * filename: util.h
 * function:
 * description:
 ***********************************************/

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

#include "macro.h"

/**
 * @brief Perform an incremental update of a 16-bit Internet checksum
 *        when a single 16-bit word in the packet has changed.
 *
 * This function is based on the incremental checksum update algorithm
 * described in RFC 1624. It avoids recalculating the entire checksum
 * from scratch, which improves performance when modifying only a small
 * part of the header (e.g., TTL or IP addresses).
 *
 * @param old_cksum
 *   The original checksum value (in host byte order).
 * @param old_word
 *   The original 16-bit word value that is being replaced (in host byte order).
 * @param new_word
 *   The new 16-bit word value that replaces the old one (in host byte order).
 *
 * @return
 *   The updated checksum value (in host byte order).
 */
static INLINE uint16_t util_cksum_incr_update(uint16_t old_cksum, uint16_t old_word, uint16_t new_word)
{
    uint32_t cksum = ~old_cksum & 0xFFFF;
    uint32_t m_old = ~old_word & 0xFFFF;
    uint32_t m_new = new_word;

    uint32_t sum = cksum + m_old + m_new;

    sum = (sum & 0xFFFF) + (sum >> 16);
    sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t result = (uint16_t)~sum;

    return (result == 0xFFFF) ? 0 : result;
}

/**
 * @brief Incrementally update a 16-bit Internet checksum based on multiple word changes.
 *
 * This function applies RFC 1624 checksum update logic to efficiently compute
 * a new checksum when multiple 16-bit fields in a packet are modified.
 * It avoids recomputing the entire checksum from scratch.
 *
 * @param old_cksum
 *   The original checksum value (host byte order).
 * @param word
 *   A pointer to an array of 16-bit word pairs.
 *   Each pair consists of: {old_word, new_word}, in host byte order.
 *   The total array size should be 2 * count.
 * @param count
 *   The number of word replacements to apply.
 *
 * @return
 *   The updated checksum (host byte order) after applying all changes.
 */
static INLINE uint16_t util_cksum_incr_update_multi(uint16_t old_cksum, const uint16_t *word, int count)
{
    uint32_t cksum = ~old_cksum & 0xFFFF;

    for (int i = 0; i < count; i++) {
        uint16_t old_word = word[2 * i];
        uint16_t new_word = word[2 * i + 1];

        cksum += (~old_word & 0xFFFF) + new_word;
        cksum = (cksum & 0xFFFF) + (cksum >> 16);
    }

    cksum = (cksum & 0xFFFF) + (cksum >> 16);
    uint16_t result = (uint16_t)~cksum;

    return (result == 0xFFFF) ? 0 : result;
}

#endif // __UTIL_H__