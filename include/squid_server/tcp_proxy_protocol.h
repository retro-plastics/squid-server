/*
 * tcp_proxy_protocol.h
 *
 * Compact binary raw-TCP proxy protocol for squid clients.
 * Multi-byte integers are little-endian.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#ifndef SQUID_SERVER_TCP_PROXY_PROTOCOL_H
#define SQUID_SERVER_TCP_PROXY_PROTOCOL_H

/*
 * Scope and wire conventions
 * --------------------------
 * This plugin proxies one outbound raw TCP byte stream. It does not implement
 * UDP, TLS, HTTP, listening sockets, or multiple simultaneous connections.
 * Application payload is never interpreted; only the proxy control packets
 * described below are binary framed.
 *
 * One squid packet carries one request or response. All u16 values are
 * unsigned little-endian. Every response starts with
 * [request_opcode | 0x80][status]. A non-OK response contains only those two
 * bytes. A normal READ timeout is not an error: it is an OK response with no
 * data and no EOF flag.
 */
#define SQUID_TCP_PROTOCOL_VERSION 1U

/* Request opcodes and response marker. */
#define SQUID_TCP_OP_CAPABILITIES 0x00U
#define SQUID_TCP_OP_CONNECT      0x01U
#define SQUID_TCP_OP_WRITE        0x02U
#define SQUID_TCP_OP_READ         0x03U
#define SQUID_TCP_OP_CLOSE        0x04U
#define SQUID_TCP_OP_STATUS       0x05U
#define SQUID_TCP_RESPONSE_BIT    0x80U

/* Status in response byte 1. */
#define SQUID_TCP_STATUS_OK             0x00U
#define SQUID_TCP_STATUS_BAD_REQUEST    0x01U
#define SQUID_TCP_STATUS_NOT_CONNECTED  0x02U
#define SQUID_TCP_STATUS_DNS_FAILED     0x03U
#define SQUID_TCP_STATUS_CONNECT_FAILED 0x04U
#define SQUID_TCP_STATUS_IO_ERROR       0x05U
#define SQUID_TCP_STATUS_TIMEOUT        0x06U
#define SQUID_TCP_STATUS_DENIED         0x07U

/* CAPABILITIES response feature bits. */
#define SQUID_TCP_FEATURE_IPV4 0x01U
#define SQUID_TCP_FEATURE_IPV6 0x02U
#define SQUID_TCP_FEATURE_DNS  0x04U

/* Address family selected by a successful CONNECT. */
#define SQUID_TCP_FAMILY_UNKNOWN 0x00U
#define SQUID_TCP_FAMILY_IPV4    0x04U
#define SQUID_TCP_FAMILY_IPV6    0x06U

/* READ response flag: the peer closed and the local socket is now closed. */
#define SQUID_TCP_READ_EOF 0x01U

/* Wire limits for a current 255-byte squid payload. */
#define SQUID_TCP_HOST_MAX 240U
#define SQUID_TCP_READ_HEADER_SIZE 4U
#define SQUID_TCP_READ_WAIT_MAX_MS 10000U

/*
 * Requests and responses
 * ----------------------
 * CAPABILITIES request:
 *   [opcode]
 *
 * CAPABILITIES response (7 bytes):
 *   [opcode|0x80][status][version:u8][features:u8]
 *   [maximum_host:u8][maximum_read_data:u8][maximum_write_data:u8]
 *   With a 255-byte response buffer, version 1 returns:
 *     80 00 01 07 f0 fb fd
 *
 * CONNECT request:
 *   [opcode][port:u16][host_len:u8][host]
 *   port is little-endian and must be 1-65535. host is a hostname, IPv4
 *   address, or IPv6 address with no NUL terminator. Whitespace, '/', '\\',
 *   and embedded NUL bytes are invalid.
 *
 * CONNECT response (3 bytes):
 *   [opcode|0x80][status][family:u8]
 *   family is SQUID_TCP_FAMILY_IPV4 or SQUID_TCP_FAMILY_IPV6. A successful
 *   CONNECT replaces the old active stream. A failed CONNECT leaves an old
 *   stream unchanged. DNS may yield several addresses; the server tries them
 *   until one connects.
 *
 * WRITE request:
 *   [opcode][data_len:u8][data]
 *   data_len must be 1-253 in a current packet.
 *
 * WRITE response (3 bytes):
 *   [opcode|0x80][status][bytes_written:u8]
 *   TCP writes may be short. Remove the acknowledged prefix and send the
 *   remainder again. One WRITE is not a remote message boundary because TCP
 *   is a byte stream.
 *
 * READ request (4 bytes):
 *   [opcode][maximum_wait_ms:u16][maximum_bytes:u8]
 *   maximum_wait_ms must not exceed SQUID_TCP_READ_WAIT_MAX_MS. Zero performs
 *   a nonblocking poll. maximum_bytes == 0 requests the largest chunk that
 *   fits, currently 251 bytes.
 *
 * READ response:
 *   [opcode|0x80][status][flags:u8][data_len:u8][data]
 *   flags == 0 and data_len == 0 means no data arrived before the requested
 *   wait expired. SQUID_TCP_READ_EOF with data_len zero means the peer closed
 *   cleanly; the plugin closes its socket, so a later READ returns
 *   SQUID_TCP_STATUS_NOT_CONNECTED. Reserved flag bits must be ignored.
 *
 * CLOSE request and response:
 *   request:  [opcode]
 *   response: [opcode|0x80][status]
 *   CLOSE is idempotent and returns OK even when no stream existed.
 *
 * STATUS request and response:
 *   request:  [opcode]
 *   response: [opcode|0x80][status][connected:u8]
 *   connected is 1 when a local socket is active and 0 otherwise. Remote
 *   failure may only become visible on the next READ or WRITE.
 *
 * Timeouts and destination control
 * --------------------------------
 * SQUID_TCP_CONNECT_TIMEOUT_MS and SQUID_TCP_WRITE_TIMEOUT_MS default to 5000
 * and are capped at 60000. The READ wait is supplied per request. DNS lookup
 * uses the Linux resolver and is not covered by the connect timeout.
 *
 * SQUID_TCP_ALLOWED_HOSTS optionally contains a comma-separated list of exact
 * host strings. Matching is case-insensitive and occurs before DNS lookup.
 * An empty/unset value or "*" allows all hosts. This is not an IP firewall:
 * an allowed hostname may resolve to any address. An unrestricted proxy can
 * reach LAN and loopback services visible to the Linux PC, so use firewall
 * rules and a host allowlist when the squid peer is not fully trusted.
 */

#endif
