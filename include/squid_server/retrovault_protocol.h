/*
 * retrovault_protocol.h
 *
 * Compact binary wire protocol for the Retro Vault squid plugin.  Multi-byte
 * integers are little-endian so Z80 clients can decode them cheaply.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#ifndef SQUID_SERVER_RETROVAULT_PROTOCOL_H
#define SQUID_SERVER_RETROVAULT_PROTOCOL_H

#include <stdint.h>

#define RETRO_VAULT_PROTOCOL_VERSION 1U

/* Request opcodes. Responses set bit 7 on the request opcode. */
#define RETRO_VAULT_OP_CAPABILITIES 0x00U
#define RETRO_VAULT_OP_LIST         0x01U
#define RETRO_VAULT_OP_SEARCH       0x02U
#define RETRO_VAULT_OP_INFO         0x03U
#define RETRO_VAULT_OP_DOWNLOAD     0x04U
#define RETRO_VAULT_RESPONSE_BIT    0x80U

/* Byte 1 in every response. */
#define RETRO_VAULT_STATUS_OK              0x00U
#define RETRO_VAULT_STATUS_BAD_REQUEST     0x01U
#define RETRO_VAULT_STATUS_API_UNAVAILABLE 0x02U
#define RETRO_VAULT_STATUS_NOT_FOUND       0x03U
#define RETRO_VAULT_STATUS_TOO_LARGE       0x04U
#define RETRO_VAULT_STATUS_INTERNAL_ERROR  0x05U

/* End marker for the 16-bit LIST, SEARCH, and INFO cursors. */
#define RETRO_VAULT_CURSOR_END 0xFFFFU

/* Capability feature bits. */
#define RETRO_VAULT_FEATURE_LIST     0x01U
#define RETRO_VAULT_FEATURE_SEARCH   0x02U
#define RETRO_VAULT_FEATURE_INFO     0x04U
#define RETRO_VAULT_FEATURE_DOWNLOAD 0x08U

/* INFO response TLV types. */
#define RETRO_VAULT_INFO_ID            0x01U
#define RETRO_VAULT_INFO_NAME          0x02U
#define RETRO_VAULT_INFO_VENDOR        0x03U
#define RETRO_VAULT_INFO_PLATFORM_ID   0x04U
#define RETRO_VAULT_INFO_PLATFORM_NAME 0x05U
#define RETRO_VAULT_INFO_VERSION       0x06U
#define RETRO_VAULT_INFO_YEAR          0x07U /* uint16_t */
#define RETRO_VAULT_INFO_RATING        0x08U /* one byte */
#define RETRO_VAULT_INFO_DESCRIPTION   0x09U
#define RETRO_VAULT_INFO_DOWNLOAD      0x0AU

/* Rating byte values used by RETRO_VAULT_INFO_RATING. */
#define RETRO_VAULT_RATING_UNKNOWN   0U
#define RETRO_VAULT_RATING_CURIOUS   1U
#define RETRO_VAULT_RATING_AVERAGE   2U
#define RETRO_VAULT_RATING_GREAT     3U
#define RETRO_VAULT_RATING_LEGENDARY 4U

/* Unknown aggregate download size in an INFO download TLV. */
#define RETRO_VAULT_SIZE_UNKNOWN UINT32_C(0xFFFFFFFF)

/*
 * Requests
 * --------
 * CAPABILITIES:
 *   [opcode]
 *
 * LIST:
 *   [opcode][cursor:u16][platform_len:u8][platform UTF-8]
 *   An empty platform lists every platform.
 *
 * SEARCH:
 *   [opcode][cursor:u16][platform_len:u8][platform UTF-8]
 *   [query_len:u8][query UTF-8]
 *
 * INFO:
 *   [opcode][cursor:u16][package_id_len:u8][package_id]
 *
 * DOWNLOAD:
 *   [opcode][offset:u32][maximum_bytes:u8]
 *   [package_id_len:u8][package_id]
 *   [download_id_len:u8][download_id]
 *   maximum_bytes == 0 requests the largest chunk that fits.
 *
 * LIST/SEARCH responses
 * ---------------------
 *   [opcode|0x80][status][next_cursor:u16][entry_count:u8]
 *   repeated entry_count times:
 *     [id_len:u8][id][name_len:u8][name]
 *
 * INFO responses
 * --------------
 *   [opcode|0x80][status][next_cursor:u16][tlv_count:u8]
 *   repeated tlv_count times:
 *     [type:u8][length:u8][value]
 *
 *   Strings are raw UTF-8. YEAR is a little-endian uint16_t. RATING is one
 *   byte. DOWNLOAD values contain:
 *     [id_len][id][label_len][label][format_len][format]
 *     [aggregate_size:u32][file_count:u8]
 *
 * DOWNLOAD responses
 * ------------------
 *   [opcode|0x80][status][offset:u32][total_size:u32][data_len:u8][data]
 *
 * Error responses contain only [opcode|0x80][status].
 */

#endif
