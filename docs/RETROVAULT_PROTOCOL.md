# Retro Vault squid binary protocol

Version 1, implemented by `libretrovault.so`.

This is the wire contract between an 8-bit package-manager client and the
Retro Vault plugin in squid-server. It is deliberately binary and small. The
client never sends HTTP and never receives JSON; HTTP, JSON parsing, catalog
filtering, and download assembly all happen on the Linux server.

The constants are also available in
`include/squid_server/retrovault_protocol.h`.

## 1. Wire conventions

- Each squid payload contains exactly one request or one response.
- The current maximum squid payload is 254 bytes.
- All multi-byte integers are unsigned and little-endian.
- Every text value is a one-byte length followed by that many UTF-8 bytes.
- Text has no trailing zero and is never escaped or padded.
- Package and download IDs should be treated as opaque, case-insensitive
  identifiers. Download requests accept ASCII letters, digits, `-`, and `_`
  in IDs. The ID is the program slug from the catalog (`lunatik`,
  `manic-miner`), not a display name.
- A response opcode is its request opcode with bit 7 set.
- A successful response has status `0x00` in byte 1.
- An error response is always just two bytes: opcode and status.
- Clients must validate every received length against the actual packet size.
  Do not map packets directly onto C structs because compiler padding and host
  byte order are not part of the protocol.

In the tables below, `u8`, `u16`, and `u32` mean one-, two-, and four-byte
unsigned integers. `bytes[n]` means exactly `n` uninterpreted bytes.

## 2. Opcodes

| Value | Request | Response | Purpose |
|---:|---|---:|---|
| `0x00` | `CAPABILITIES` | `0x80` | Discover protocol version and sizes |
| `0x01` | `LIST` | `0x81` | List packages, optionally by platform |
| `0x02` | `SEARCH` | `0x82` | Search packages, optionally by platform |
| `0x03` | `INFO` | `0x83` | Read package metadata and download choices |
| `0x04` | `DOWNLOAD` | `0x84` | Read a download in binary chunks |

Unknown request opcodes receive `opcode | 0x80` and status `BAD_REQUEST`.

## 3. Status codes

| Value | Name | Meaning |
|---:|---|---|
| `0x00` | `OK` | Request succeeded |
| `0x01` | `BAD_REQUEST` | Invalid opcode, length, cursor, ID, or offset |
| `0x02` | `API_UNAVAILABLE` | Retro Vault could not be reached |
| `0x03` | `NOT_FOUND` | Package, download, or API resource was not found |
| `0x04` | `TOO_LARGE` | API data exceeded the server-side safety limit |
| `0x05` | `INTERNAL_ERROR` | Malformed upstream data or another server error |

When status is not `OK`, the response ends after byte 1. There is no text error
message to waste bandwidth or require a character set on the client.

## 4. Capabilities

Send this once after opening the plugin channel. Version 1 has all four feature
bits set.

### Request

| Offset | Size | Field | Required value |
|---:|---:|---|---:|
| 0 | 1 | opcode | `0x00` |

Request size: 1 byte.

### Successful response

| Offset | Size | Field | Version 1 value |
|---:|---:|---|---:|
| 0 | 1 | response opcode | `0x80` |
| 1 | 1 | status | `0x00` |
| 2 | 1 | protocol version | `0x01` |
| 3 | 1 | feature bits | see below |
| 4 | 1 | list/info page-header size | `5` |
| 5 | 1 | download response-header size | `11` |
| 6 | 1 | maximum download data bytes | normally `243` |

Feature bits are `0x01` LIST, `0x02` SEARCH, `0x04` INFO, and `0x08`
DOWNLOAD. Unknown feature bits must be ignored.

With a 254-byte squid payload, the complete exchange in hexadecimal is:

```text
request:  00
response: 80 00 01 0f 05 0b f3
```

## 5. Cursors and page headers

LIST, SEARCH, and INFO use 16-bit cursors to keep every response within one
squid packet. Start with cursor `0x0000`. If the response returns another
cursor, repeat the same request with that cursor. A returned cursor of
`0xffff` means end of results and must not be sent back to a paged operation.

The common successful page header is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode |
| 1 | 1 | status `0x00` |
| 2 | 2 | next cursor, little-endian; `0xffff` means end |
| 4 | 1 | number of entries or TLVs in this packet |

LIST and SEARCH cursors are zero-based positions in the matching result set.
INFO cursors are zero-based metadata-record positions. A cursor is meaningful
only with the exact same platform, query, or package ID used on the preceding
page.

The catalog is cached for one hour, which normally keeps a paged traversal
stable. If the upstream catalog changes exactly when that cache refreshes,
restart the traversal at cursor zero if results appear inconsistent.

## 6. List packages

LIST returns compact ID/name pairs. An empty platform includes every platform.
Platform matching is case-insensitive. `idp` and `iskra-delta-partner` are the
same platform, as are `zxs` and `zx-spectrum`.

An optional model filter may follow the platform. An omitted or empty model
includes every model. If a model is given, only titles with that model are
returned. Partner model `p` has no catalog entries yet, so a `p` filter is
an empty page until that catalog exists.

### Request

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x01` |
| 1 | 2 | cursor |
| 3 | 1 | platform length `P` |
| 4 | `P` | platform ID bytes; absent when `P = 0` |
| `4 + P` | 1 | optional model length `M` |
| `5 + P` | `M` | optional model ID bytes |

Request size: `4 + P` bytes, or `5 + P + M` when a model is present.

For example, list ZX Spectrum packages from the beginning:

```text
01 00 00 03 7a 78 73
```

List Iskra Delta Partner GDP packages from the beginning:

```text
01 00 00 03 69 64 70 03 67 64 70
```

### Successful response

The five-byte page header is followed by `count` entries:

| Relative offset | Size | Entry field |
|---:|---:|---|
| 0 | 1 | package ID length `I` |
| 1 | `I` | package ID |
| `1 + I` | 1 | package name length `N` |
| `2 + I` | `N` | package name |

Entry size: `2 + I + N` bytes. There are no separators.

An illustrative one-entry final page containing ID `manic-miner` and name
`Manic Miner` is:

```text
81 00 ff ff 01 0b 6d 61 6e 69 63 2d 6d 69 6e 65 72
0b 4d 61 6e 69 63 20 4d 69 6e 65 72
```

If one name alone is too long for a packet, the server shortens the name at a
UTF-8 character boundary. The ID is never shortened.

## 7. Search packages

SEARCH uses the same response records and cursor rules as LIST. Matching is
case-insensitive and covers package ID, name, vendor, and description. The
platform filter is applied first, then the optional model filter. The query
must contain at least one byte.

### Request

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x02` |
| 1 | 2 | cursor |
| 3 | 1 | platform length `P` |
| 4 | `P` | platform ID bytes; absent when `P = 0` |
| `4 + P` | 1 | query length `Q` |
| `5 + P` | `Q` | query bytes |
| `5 + P + Q` | 1 | optional model length `M` |
| `6 + P + Q` | `M` | optional model ID bytes |

Request size: `5 + P + Q` bytes, or `6 + P + Q + M` when a model is present.

For example, search all platforms for `iskra`:

```text
02 00 00 00 05 69 73 6b 72 61
```

## 8. Package information

INFO returns typed length/value records. A client may ignore unknown types by
skipping the advertised value length.

### Request

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x03` |
| 1 | 2 | cursor |
| 3 | 1 | package ID length `I` |
| 4 | `I` | package ID bytes |

Request size: `4 + I` bytes. `I` must not be zero.

For package `manic-miner`, the first request is:

```text
03 00 00 0b 6d 61 6e 69 63 2d 6d 69 6e 65 72
```

### Successful response

The five-byte page header is followed by `count` TLVs:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type |
| 1 | 1 | value length `L` |
| 2 | `L` | value |

TLV size: `2 + L` bytes. A TLV is never split across packets. Long string
values may be shortened at a UTF-8 character boundary so that one whole TLV
fits in a packet.

Records have a fixed cursor order:

| Cursor | Type | Value | Encoding |
|---:|---:|---|---|
| 0 | `0x01` | package ID | UTF-8 bytes |
| 1 | `0x02` | name | UTF-8 bytes |
| 2 | `0x03` | vendor | UTF-8 bytes |
| 3 | `0x04` | platform ID | UTF-8 bytes |
| 4 | `0x05` | platform name | UTF-8 bytes |
| 5 | `0x06` | version | UTF-8 bytes |
| 6 | `0x07` | release year | `u16`; zero means unknown |
| 7 | `0x08` | rating | `u8`; see below |
| 8 | `0x09` | description | UTF-8 bytes |
| 9 onward | `0x0a` | one download choice | nested binary record |

Rating values are:

| Value | Meaning |
|---:|---|
| 0 | unknown |
| 1 | Curious |
| 2 | Average |
| 3 | Great |
| 4 | Legendary |

For example, the NAME TLV for `Manic Miner` is:

```text
02 0b 4d 61 6e 69 63 20 4d 69 6e 65 72
```

### Download-choice TLV

Type `0x0a` uses the following value; its outer TLV length covers every byte
shown here:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | download ID length `D` |
| 1 | `D` | download ID |
| `1 + D` | 1 | label length `L` |
| `2 + D` | `L` | display label |
| `2 + D + L` | 1 | format length `F` |
| `3 + D + L` | `F` | format, such as `TZX` or `ADF` |
| `3 + D + L + F` | 4 | aggregate source size |
| `7 + D + L + F` | 1 | file count |

Aggregate size `0xffffffff` means unknown. For a multi-file choice, this value
is the sum of the source file sizes, not the size of the ZIP returned by the
API. The `total_size` in DOWNLOAD is authoritative for the actual byte stream.
File count saturates at 255.

## 9. Download package data

DOWNLOAD transfers the selected download as raw binary chunks. The client must
obtain the package and download IDs from LIST/SEARCH and INFO; it does not send
a URL or filename.

### Request

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x04` |
| 1 | 4 | requested byte offset |
| 5 | 1 | maximum returned data bytes `M` |
| 6 | 1 | package ID length `P` |
| 7 | `P` | package ID |
| `7 + P` | 1 | download ID length `D` |
| `8 + P` | `D` | download ID |

Request size: `8 + P + D` bytes. Set `M` to zero to request the largest chunk
the packet can hold. Otherwise, the server returns no more than `M` data bytes.

For `manic-miner`, choice `complete`, offset zero, and at most 20 bytes:

```text
04 00 00 00 00 14 0b 6d 61 6e 69 63 2d 6d 69 6e 65 72
08 63 6f 6d 70 6c 65 74 65
```

### Successful response

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x84` |
| 1 | 1 | status `0x00` |
| 2 | 4 | returned offset; must equal the requested offset |
| 6 | 4 | total byte-stream size |
| 10 | 1 | data length `N` |
| 11 | `N` | raw package bytes |

Response size: `11 + N` bytes. With the current 254-byte payload, `N` is at
most 243 (`0xf3`). The final chunk may be shorter. Requesting an offset equal
to `total_size` succeeds with `N = 0`; an offset larger than `total_size` is a
bad request.

For a 600-byte object, a 20-byte first chunk beginning with byte values 0
through 19 is:

```text
84 00 00 00 00 00 58 02 00 00 14
00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13
```

Continue at `requested_offset + N` until that offset equals `total_size`.
Never assume the INFO aggregate size equals `total_size`.

The first request for a download looks the package up in the cached catalog
and fetches the complete API result. Titles with a model in the catalog use
the canonical path including that model. Partner titles today are all `gdp`;
model `p` has no entries yet. Titles with no model omit that path segment.

```text
GET /api/v1/catalog/packages/{platformId}/{modelId}/{id}/downloads/{downloadId}
GET /api/v1/catalog/packages/idp/gdp/lunatik/downloads/complete
```

Titles without a model (Spectrum and the others today) omit that segment:

```text
GET /api/v1/catalog/packages/{platformId}/{id}/downloads/{downloadId}
GET /api/v1/catalog/packages/zxs/manic-miner/downloads/complete
```

If the catalog entry has no platform, the plugin falls back to the unique-id
path `/api/v1/catalog/packages/{id}/downloads/{downloadId}`. A `409` from that
form is retried with `?platformId=` and, when present, `&modelId=`.

A one-file choice is its original file; a multi-file choice is a ZIP produced
by Retro Vault. Subsequent chunks of the same choice come from memory.
Requesting another choice replaces that cache. The current server safety
limit is 64 MiB per cached download.

## 10. Minimal client logic

These helpers avoid alignment and packing assumptions on a typical 8-bit C
compiler where `unsigned int` is at least 16 bits and `unsigned long` is at
least 32 bits:

```c
unsigned int rv_get_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

unsigned long rv_get_u32(const unsigned char *p)
{
    return (unsigned long)p[0]
        | ((unsigned long)p[1] << 8)
        | ((unsigned long)p[2] << 16)
        | ((unsigned long)p[3] << 24);
}

void rv_put_u32(unsigned char *p, unsigned long value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}
```

A LIST, SEARCH, or INFO loop is:

1. Set cursor to zero.
2. Send the request and wait for one response.
3. Verify response opcode, status, and every length before consuming records.
4. Process exactly the count in byte 4.
5. Read the next cursor from bytes 2 and 3.
6. Stop on `0xffff`; otherwise repeat with the returned cursor.

A DOWNLOAD loop is:

1. Set offset to zero and keep the same package and download IDs.
2. Send DOWNLOAD, preferably with maximum bytes zero.
3. Verify opcode, status, returned offset, total size, and data length.
4. Write the `N` data bytes directly to storage.
5. Add `N` to the offset and repeat until offset equals total size.

## 11. Server configuration

The supplied configurations mount the plugin on squid channel 3:

```text
plugin 3 /opt/squid/lib/plugins/libretrovault.so
```

The default API base URL is `https://retro-vault.org`. It can be overridden
when starting squid-server, primarily for development:

```sh
RETRO_VAULT_API_URL=http://127.0.0.1:5000 squid-server --console
```

Only `http://` and `https://` base URLs are accepted. The catalog response is
limited to 4 MiB, cached for one hour, and retained if a later refresh fails.
