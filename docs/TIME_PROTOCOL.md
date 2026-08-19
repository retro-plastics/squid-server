# Squid PC time-and-date binary protocol

Version 1, implemented by `libtime.so` on squid channel 5.

This read-only protocol returns the Linux PC's date and time in one fixed-size
binary record. It provides both UTC and the PC's configured local timezone.
There are no formatted date strings for the 8-bit client to parse.

All multi-byte integers are little-endian. Constants are mirrored in
`include/squid_server/time_protocol.h`.

## 1. Opcodes and status

| Request | Response | Name |
|---:|---:|---|
| `0x00` | `0x80` | CAPABILITIES |
| `0x01` | `0x81` | GET |

| Status | Name | Meaning |
|---:|---|---|
| `0x00` | OK | Time was read successfully |
| `0x01` | BAD_REQUEST | Invalid opcode, mode, or packet length |
| `0x02` | UNAVAILABLE | The PC time or timezone could not be read |

An error response is exactly two bytes: response opcode and status.

## 2. Capabilities

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
| 4 | 1 | GET response size | `16` |

Feature bits are UTC `0x01`, local time `0x02`, and Unix seconds `0x04`.

Complete response:

```text
80 00 01 07 10
```

## 3. Read time and date

GET request size is two bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x01` |
| 1 | 1 | mode: UTC `0x00` or local `0x01` |

Read UTC:

```text
01 00
```

Read the PC's configured local time:

```text
01 01
```

The successful response is always 16 bytes:

| Offset | Size | Field | Range or meaning |
|---:|---:|---|---|
| 0 | 1 | response opcode | `0x81` |
| 1 | 1 | status | `0x00` |
| 2 | 2 | year | full year, for example 2026 |
| 4 | 1 | month | 1 through 12 |
| 5 | 1 | day of month | 1 through 31 |
| 6 | 1 | hour | 0 through 23 |
| 7 | 1 | minute | 0 through 59 |
| 8 | 1 | second | 0 through 60; 60 permits a leap second |
| 9 | 1 | weekday and DST | described below |
| 10 | 2 | UTC offset minutes | signed two's-complement `i16` |
| 12 | 4 | Unix seconds | or `0xffffffff` when unavailable |

Weekday is in bits 0-2: Sunday `0`, Monday `1`, through Saturday `6`. Bit 7
is set when local daylight-saving time is active. Other bits are reserved and
must be ignored.

UTC mode always reports offset zero and a clear DST bit. Local mode applies
the Linux PC's current timezone and DST rules. The Unix-seconds field denotes
the same instant in both modes; only the broken-down calendar fields differ.

The UTC offset is minutes east of UTC. Examples:

- `00 00` means UTC.
- `3c 00` means UTC+60 minutes.
- `c4 ff` is signed `-60`, meaning UTC-60 minutes.

An illustrative UTC record for Wednesday, 19 August 2026 at 12:34:56 is:

```text
81 00 ea 07 08 13 0c 22 38 03 00 00 ff ff ff ff
```

The example uses the allowed `0xffffffff` unknown value for Unix seconds.
Normal contemporary PCs return a real 32-bit Unix value.

## 4. Minimal client rules

1. Send GET with the required mode.
2. Require exactly 16 bytes for an OK response.
3. Decode year and UTC offset as little-endian 16-bit values.
4. Sign-extend the UTC offset when storing it in a larger integer.
5. Mask weekday with `0x07`; test DST with `0x80`.
6. Treat Unix value `0xffffffff` as unavailable and use the calendar fields.

The plugin only reads time. It cannot set the PC time, timezone, or locale.
Its accuracy is exactly the accuracy of the Linux host time source.

## 5. Server configuration

```text
plugin 5 /opt/squid/lib/plugins/libtime.so
```

No plugin-specific environment variables are required. Configure the Linux
timezone normally, for example through `/etc/localtime` or your distribution's
time configuration tool.
