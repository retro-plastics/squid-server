# Squid raw TCP proxy binary protocol

Version 1, implemented by `libtcp_proxy.so` on squid channel 6.

This protocol lets a squid client open one outbound TCP stream through the
Linux PC, then exchange raw bytes. Control messages are compact binary packets;
the stream payload is not interpreted and may contain binary or text belonging
to the remote application protocol.

This is TCP only. It does not implement UDP, TLS, HTTP, DNS packets, listening
sockets, or multiple simultaneous connections. A client can speak an
application protocol over the byte stream and can implement TLS itself if its
hardware is capable of doing so.

Multi-byte integers are unsigned little-endian. Constants are mirrored in
`include/squid_server/tcp_proxy_protocol.h`.

## 1. Security warning

By default the plugin allows connection to any hostname or numeric address.
That is intentional for a general Internet proxy, but it also permits access
to services reachable only from the Linux PC, including LAN and loopback
services. Only expose the squid transport to trusted machines.

For a restricted appliance, set `SQUID_TCP_ALLOWED_HOSTS` to a comma-separated
list of exact hostnames or numeric addresses. Matching is case-insensitive;
wildcard suffixes are not supported. The special value `*` and an unset or
empty variable mean allow all.

## 2. Opcodes

| Request | Response | Name | Purpose |
|---:|---:|---|---|
| `0x00` | `0x80` | CAPABILITIES | Discover version and limits |
| `0x01` | `0x81` | CONNECT | Open or replace the active TCP stream |
| `0x02` | `0x82` | WRITE | Send raw stream bytes |
| `0x03` | `0x83` | READ | Receive raw stream bytes |
| `0x04` | `0x84` | CLOSE | Close the active stream |
| `0x05` | `0x85` | STATUS | Test whether a stream is active |

## 3. Status codes

| Value | Name | Meaning |
|---:|---|---|
| `0x00` | OK | Operation succeeded |
| `0x01` | BAD_REQUEST | Invalid opcode, length, host, port, or wait time |
| `0x02` | NOT_CONNECTED | READ or WRITE has no active stream |
| `0x03` | DNS_FAILED | Hostname lookup failed |
| `0x04` | CONNECT_FAILED | No resolved address accepted the connection |
| `0x05` | IO_ERROR | The stream failed and was closed |
| `0x06` | TIMEOUT | CONNECT or WRITE exceeded its configured timeout |
| `0x07` | DENIED | Host is not on the configured allowlist |

An error response is exactly two bytes. A READ wait that expires normally is
not an error: it returns OK with zero data so a client can poll cheaply.

## 4. Capabilities

Request:

```text
00
```

Successful response:

| Offset | Size | Field | Version 1 value |
|---:|---:|---|---:|
| 0 | 1 | response opcode | `0x80` |
| 1 | 1 | status | `0x00` |
| 2 | 1 | protocol version | `1` |
| 3 | 1 | feature bits | `0x07` |
| 4 | 1 | maximum host bytes | `240` |
| 5 | 1 | maximum READ data bytes | normally `251` |
| 6 | 1 | maximum WRITE data bytes | normally `253` |

Feature bits are IPv4 `0x01`, IPv6 `0x02`, and hostname lookup `0x04`.

With a 255-byte squid payload:

```text
80 00 01 07 f0 fb fd
```

## 5. CONNECT

Request size is `4 + H`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x01` |
| 1 | 2 | TCP port, 1 through 65535 |
| 3 | 1 | host length `H` |
| 4 | `H` | hostname, IPv4 address, or IPv6 address |

Host bytes are not zero-terminated. Whitespace, `/`, `\`, and zero bytes are
rejected. Ports are little-endian on the squid wire even though TCP itself uses
network byte order internally.

Connect to `retro-vault.org` on port 80:

```text
01 50 00 0f 72 65 74 72 6f 2d 76 61 75 6c 74 2e 6f 72 67
```

Successful response size is three bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x81` |
| 1 | 1 | status `0x00` |
| 2 | 1 | selected family: IPv4 `4` or IPv6 `6` |

A successful CONNECT closes the previous active stream and installs the new
one. A failed CONNECT leaves an existing stream unchanged. Host lookup may
produce several addresses; the plugin tries them until one connects.

The default connect timeout is 5000 ms per address and is capped at 60000 ms.
Set `SQUID_TCP_CONNECT_TIMEOUT_MS` to change it.

## 6. WRITE

Request size is `2 + N`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x02` |
| 1 | 1 | data length `N`, 1 through 253 |
| 2 | `N` | raw stream bytes |

Successful response size is three bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x82` |
| 1 | 1 | status `0x00` |
| 2 | 1 | bytes actually written |

TCP writes may be short. Remove the acknowledged prefix from the client buffer
and send the remainder in another WRITE. Do not assume one WRITE corresponds
to one remote message; TCP is a byte stream.

The default write-ready timeout is 5000 ms and is capped at 60000 ms. Set
`SQUID_TCP_WRITE_TIMEOUT_MS` to change it.

Example writing four application bytes `ping`:

```text
request:  02 04 70 69 6e 67
response: 82 00 04
```

## 7. READ

Request size is four bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x03` |
| 1 | 2 | maximum wait in milliseconds |
| 3 | 1 | maximum data bytes `M`; zero means packet maximum |

The maximum wait is 10000 ms. Use zero for a nonblocking poll. A modest wait
reduces request traffic while waiting for a slow Internet server, but the
squid-server dispatch loop cannot serve another plugin while this READ waits.

Wait up to 1000 ms and accept the largest chunk:

```text
03 e8 03 00
```

Successful response:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x83` |
| 1 | 1 | status `0x00` |
| 2 | 1 | flags |
| 3 | 1 | data length `N` |
| 4 | `N` | raw stream bytes |

Flag bit `0x01` means EOF: the remote peer closed cleanly and the plugin has
closed its socket. Reserved flag bits must be ignored. `N` is at most 251.

There are three normal zero-length cases:

- flags `0`, length `0`: no data arrived before the requested wait expired;
- EOF flag, length `0`: the remote peer closed cleanly;
- a later request after EOF: NOT_CONNECTED, because the socket is gone.

Example receiving `pong`:

```text
83 00 00 04 70 6f 6e 67
```

TCP can split or combine application messages arbitrarily. The client must
frame the remote application protocol itself.

## 8. CLOSE and STATUS

CLOSE request is one byte `04`. It is idempotent; successful response is
`84 00` whether or not a stream existed.

STATUS request is one byte `05`. Successful response is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x85` |
| 1 | 1 | status `0x00` |
| 2 | 1 | `1` when a stream is active, otherwise `0` |

STATUS reports local socket state. TCP failure may only become visible on the
next READ or WRITE.

## 9. Server configuration

```text
plugin 6 /opt/squid/lib/plugins/libtcp_proxy.so
```

Example restricted environment:

```sh
SQUID_TCP_ALLOWED_HOSTS=retro-vault.org,example.com,192.0.2.10
SQUID_TCP_CONNECT_TIMEOUT_MS=5000
SQUID_TCP_WRITE_TIMEOUT_MS=5000
```

The allowlist is checked against the host string sent by the client before DNS
resolution. It is not an IP firewall: an allowed hostname can resolve to any
address. Use Linux firewall rules as well when destination-level isolation is
required.
