/*
 * time_protocol.h
 *
 * Fixed-size binary PC time-and-date protocol for squid clients.
 * Multi-byte integers are little-endian.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#ifndef SQUID_SERVER_TIME_PROTOCOL_H
#define SQUID_SERVER_TIME_PROTOCOL_H

/*
 * Wire conventions
 * ----------------
 * One squid packet carries one request or response. All multi-byte fields are
 * little-endian. Every response starts [request_opcode | 0x80][status]. A
 * non-OK response ends after those two bytes. The successful GET record is a
 * fixed 16 bytes, so an 8-bit client needs no string, locale, or variable-size
 * date parser.
 */
#define SQUID_TIME_PROTOCOL_VERSION 1U

/* Request opcodes and response marker. */
#define SQUID_TIME_OP_CAPABILITIES 0x00U
#define SQUID_TIME_OP_GET          0x01U
#define SQUID_TIME_RESPONSE_BIT    0x80U

/* Status in response byte 1. */
#define SQUID_TIME_STATUS_OK          0x00U
#define SQUID_TIME_STATUS_BAD_REQUEST 0x01U
#define SQUID_TIME_STATUS_UNAVAILABLE 0x02U

/* GET request mode in byte 1. */
#define SQUID_TIME_MODE_UTC   0x00U
#define SQUID_TIME_MODE_LOCAL 0x01U

/* CAPABILITIES response feature bits. */
#define SQUID_TIME_FEATURE_UTC   0x01U
#define SQUID_TIME_FEATURE_LOCAL 0x02U
#define SQUID_TIME_FEATURE_UNIX  0x04U

/* GET response byte 9: weekday in bits 0-2 and local DST in bit 7. */
#define SQUID_TIME_WEEKDAY_MASK 0x07U
#define SQUID_TIME_DST_BIT      0x80U

/* GET response Unix seconds uses this value when it cannot fit in a u32. */
#define SQUID_TIME_UNIX_UNKNOWN 0xFFFFFFFFU

/* Total size of every successful GET response. */
#define SQUID_TIME_REPLY_SIZE   16U

/*
 * Requests and responses
 * ----------------------
 * CAPABILITIES request:
 *   [opcode]
 *
 * CAPABILITIES response (5 bytes):
 *   [opcode|0x80][status][version:u8][features:u8][get_reply_size:u8]
 *   Version 1 normally returns:
 *     80 00 01 07 10
 *
 * GET request (2 bytes):
 *   [opcode][mode:u8]
 *   mode is SQUID_TIME_MODE_UTC or SQUID_TIME_MODE_LOCAL.
 *
 * GET response (16 bytes):
 *   byte  0: opcode | 0x80
 *   byte  1: status
 *   bytes 2-3: full year, u16 (for example 2026)
 *   byte  4: month, 1-12
 *   byte  5: day of month, 1-31
 *   byte  6: hour, 0-23
 *   byte  7: minute, 0-59
 *   byte  8: second, 0-60 (60 permits a leap second)
 *   byte  9: weekday/DST flags
 *   bytes 10-11: signed UTC offset in minutes, little-endian i16
 *   bytes 12-15: Unix seconds, u32, or SQUID_TIME_UNIX_UNKNOWN
 *
 * Weekday values are Sunday 0 through Saturday 6. Mask byte 9 with
 * SQUID_TIME_WEEKDAY_MASK before comparing it. SQUID_TIME_DST_BIT is set only
 * for local mode when daylight-saving time is active. Reserved bits must be
 * ignored.
 *
 * The signed offset is minutes east of UTC: +60 means UTC+01:00 and -60 means
 * UTC-01:00. UTC mode always returns offset zero and a clear DST bit. Local
 * mode uses the Linux PC's current timezone rules. The Unix value denotes the
 * same instant in both modes; only the broken-down calendar fields differ.
 *
 * This protocol is read-only. It cannot change the PC time, date, timezone,
 * or locale. No plugin-specific configuration is required.
 */

#endif
