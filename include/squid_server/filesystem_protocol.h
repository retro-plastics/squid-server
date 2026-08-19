/*
 * filesystem_protocol.h
 *
 * Compact binary protocol for the rooted squid filesystem plugin.
 * Multi-byte integers are little-endian.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#ifndef SQUID_SERVER_FILESYSTEM_PROTOCOL_H
#define SQUID_SERVER_FILESYSTEM_PROTOCOL_H

/*
 * Wire conventions
 * ----------------
 * One squid packet carries one complete request or response. The current
 * payload limit is 254 bytes. All u16 and u32 fields described below are
 * unsigned little-endian values. Paths and names are counted byte strings:
 * they have an explicit u8 length and no trailing NUL.
 *
 * Every response starts with [request_opcode | 0x80][status]. When status is
 * not OK, those are the only two response bytes. Clients must not attempt to
 * decode the operation-specific fields of an error response.
 */
#define SQUID_FS_PROTOCOL_VERSION 1U

/* Request opcodes. */
#define SQUID_FS_OP_CAPABILITIES 0x00U
#define SQUID_FS_OP_STAT         0x01U
#define SQUID_FS_OP_LIST         0x02U
#define SQUID_FS_OP_READ         0x03U
#define SQUID_FS_OP_WRITE        0x04U
#define SQUID_FS_OP_MKDIR        0x05U
#define SQUID_FS_OP_DELETE       0x06U
#define SQUID_FS_OP_RENAME       0x07U
#define SQUID_FS_RESPONSE_BIT    0x80U

/* Status in response byte 1. */
#define SQUID_FS_STATUS_OK             0x00U
#define SQUID_FS_STATUS_BAD_REQUEST    0x01U
#define SQUID_FS_STATUS_NOT_FOUND      0x02U
#define SQUID_FS_STATUS_NOT_DIRECTORY  0x03U
#define SQUID_FS_STATUS_IS_DIRECTORY   0x04U
#define SQUID_FS_STATUS_ACCESS_DENIED  0x05U
#define SQUID_FS_STATUS_EXISTS         0x06U
#define SQUID_FS_STATUS_TOO_LARGE      0x07U
#define SQUID_FS_STATUS_NOT_EMPTY      0x08U
#define SQUID_FS_STATUS_READ_ONLY      0x09U
#define SQUID_FS_STATUS_NO_SPACE       0x0AU
#define SQUID_FS_STATUS_IO_ERROR       0x0BU

/* Little-endian u16 feature mask in CAPABILITIES response bytes 3-4. */
#define SQUID_FS_FEATURE_STAT   0x0001U
#define SQUID_FS_FEATURE_LIST   0x0002U
#define SQUID_FS_FEATURE_READ   0x0004U
#define SQUID_FS_FEATURE_WRITE  0x0008U
#define SQUID_FS_FEATURE_MKDIR  0x0010U
#define SQUID_FS_FEATURE_DELETE 0x0020U
#define SQUID_FS_FEATURE_RENAME 0x0040U

/* Object type returned by STAT and each LIST entry. */
#define SQUID_FS_TYPE_UNKNOWN   0x00U
#define SQUID_FS_TYPE_FILE      0x01U
#define SQUID_FS_TYPE_DIRECTORY 0x02U
#define SQUID_FS_TYPE_OTHER     0x03U

/* WRITE request flag bits. Unknown bits make the request invalid. */
#define SQUID_FS_WRITE_CREATE   0x01U
#define SQUID_FS_WRITE_TRUNCATE 0x02U

/* A returned LIST cursor of 0xffff means there are no more entries. */
#define SQUID_FS_CURSOR_END 0xFFFFU

/* A regular-file size that cannot be represented in a u32. */
#define SQUID_FS_SIZE_UNKNOWN 0xFFFFFFFFU

/* Server-side maximum relative path length, excluding a trailing NUL. */
#define SQUID_FS_PATH_MAX 240U

/*
 * Root and path rules
 * -------------------
 * Paths are always relative to SQUID_FS_ROOT (default /opt/squid/share).
 * An empty path denotes the exported root only for STAT and LIST. Absolute
 * paths, NUL bytes, empty components, ".", and ".." are rejected. The server
 * opens every intermediate directory without following symlinks, so a client
 * cannot escape the configured root through a symlink.
 *
 * The protocol is deliberately handle-free: READ and WRITE carry their path
 * and offset on every request. DELETE removes one file or one empty directory;
 * it is never recursive. SQUID_FS_READ_ONLY=1 disables WRITE, MKDIR, DELETE,
 * and RENAME, and removes those feature bits from CAPABILITIES.
 *
 * Requests and responses
 * ----------------------
 * CAPABILITIES request:
 *   [opcode]
 *
 * CAPABILITIES response (9 bytes):
 *   [opcode|0x80][status][version:u8][features:u16]
 *   [maximum_path:u8][list_header_size:u8]
 *   [read_header_size:u8][maximum_read_data:u8]
 *
 * STAT request:
 *   [opcode][path_len:u8][path]
 *   path_len may be zero to stat the exported root.
 *
 * STAT response (13 bytes):
 *   [opcode|0x80][status][type:u8][mode:u16]
 *   [size:u32][modification_unix_time:u32]
 *   mode contains only POSIX permission bits 0777. Directories and other
 *   non-regular objects report size zero. An unavailable modification time is
 *   zero; an unrepresentable regular-file size is SQUID_FS_SIZE_UNKNOWN.
 *
 * LIST request:
 *   [opcode][cursor:u16][path_len:u8][directory_path]
 *   Start with cursor zero. An empty path lists the exported root.
 *
 * LIST response:
 *   [opcode|0x80][status][next_cursor:u16][entry_count:u8]
 *   repeated entry_count times:
 *     [type:u8][name_len:u8][name][size:u32]
 *   A returned next_cursor of SQUID_FS_CURSOR_END finishes the traversal.
 *   Otherwise repeat the same path with the returned cursor. Directory order
 *   is the host filesystem's order, so restart at zero if the directory is
 *   modified during traversal.
 *
 * READ request:
 *   [opcode][offset:u32][maximum_bytes:u8][path_len:u8][path]
 *   maximum_bytes == 0 requests the largest chunk that fits.
 *
 * READ response:
 *   [opcode|0x80][status][offset:u32][total_size:u32]
 *   [data_len:u8][data]
 *   The fixed header is 11 bytes, leaving at most 243 data bytes in a current
 *   squid packet. Continue at offset + data_len. Reading exactly at EOF is OK
 *   with data_len zero; an offset beyond EOF is BAD_REQUEST.
 *
 * WRITE request:
 *   [opcode][offset:u32][flags:u8][path_len:u8][path]
 *   [data_len:u8][data]
 *   Request size is 8 + path_len + data_len, so current packets require
 *   data_len <= 246 - path_len. data_len may be zero to create or truncate.
 *
 * WRITE response (7 bytes):
 *   [opcode|0x80][status][offset:u32][bytes_written:u8]
 *   A successful write may be short; advance by bytes_written and retry the
 *   remainder. TRUNCATE is normally used only at offset zero on the first
 *   request for a file.
 *
 * MKDIR and DELETE request:
 *   [opcode][path_len:u8][path]
 *   The path must not be empty. Success is the two-byte OK response.
 *
 * RENAME request:
 *   [opcode][old_len:u8][old_path][new_len:u8][new_path]
 *   Both paths must be non-empty and remain under the same exported root.
 *   Success is the two-byte OK response. Destination replacement follows
 *   normal POSIX rename semantics.
 */

#endif
