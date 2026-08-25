# Squid rooted filesystem binary protocol

Version 1, implemented by `libfilesystem.so` on squid channel 4.

This protocol gives a small client POSIX-like file operations without open
handles or textual commands. Every operation names a path relative to one
Linux directory. Requests and responses fit in one 255-byte squid payload.
Multi-byte integers are unsigned little-endian unless stated otherwise.

The constants are mirrored in
`include/squid_server/filesystem_protocol.h`.

## 1. Design and safety rules

- The server exports only the configured root directory.
- Paths are length-prefixed bytes with no trailing zero.
- An empty path denotes the exported root where explicitly allowed.
- Absolute paths, zero bytes, empty components, `.`, and `..` are rejected.
- Each intermediate directory is opened separately with no symlink following.
  A symlink therefore cannot escape the exported root.
- There are no remote file descriptors. READ and WRITE carry a path and offset
  on every request, which keeps reconnect and error recovery simple.
- DELETE removes a file or an empty directory. It is never recursive.
- RENAME has normal POSIX replacement behavior when the destination exists.
- Error responses are exactly two bytes: response opcode and status.
- Clients must bounds-check all received lengths before using them.

The server default root is `/opt/squid/share`. `SQUID_FS_ROOT` overrides it.
Set `SQUID_FS_READ_ONLY=1` to disable every mutating operation.

## 2. Opcodes

| Request | Response | Name | Purpose |
|---:|---:|---|---|
| `0x00` | `0x80` | CAPABILITIES | Discover version, features, and limits |
| `0x01` | `0x81` | STAT | Read type, mode, size, and modification time |
| `0x02` | `0x82` | LIST | Read a directory page |
| `0x03` | `0x83` | READ | Read a file chunk |
| `0x04` | `0x84` | WRITE | Write a file chunk |
| `0x05` | `0x85` | MKDIR | Create one directory |
| `0x06` | `0x86` | DELETE | Remove one file or empty directory |
| `0x07` | `0x87` | RENAME | Rename or move inside the exported root |

## 3. Status codes

| Value | Name | Meaning |
|---:|---|---|
| `0x00` | OK | Operation succeeded |
| `0x01` | BAD_REQUEST | Invalid packet, path, flags, cursor, or offset |
| `0x02` | NOT_FOUND | A path component does not exist |
| `0x03` | NOT_DIRECTORY | A required directory component is not a directory |
| `0x04` | IS_DIRECTORY | READ was attempted on a directory |
| `0x05` | ACCESS_DENIED | Permission denied or unsupported object type |
| `0x06` | EXISTS | The requested new object already exists |
| `0x07` | TOO_LARGE | Path, directory entry, or file exceeds a limit |
| `0x08` | NOT_EMPTY | DELETE was attempted on a non-empty directory |
| `0x09` | READ_ONLY | Mutation is disabled or the Linux filesystem is read-only |
| `0x0a` | NO_SPACE | The Linux filesystem has no space or quota remaining |
| `0x0b` | IO_ERROR | Other Linux I/O failure |

For every non-OK status, stop after response byte 1. No explanatory string is
sent.

## 4. Capabilities

Request:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x00` |

Successful response:

| Offset | Size | Field | Version 1 value |
|---:|---:|---|---:|
| 0 | 1 | response opcode | `0x80` |
| 1 | 1 | status | `0x00` |
| 2 | 1 | protocol version | `1` |
| 3 | 2 | feature bits | little-endian |
| 5 | 1 | maximum path bytes | `240` |
| 6 | 1 | LIST page-header bytes | `5` |
| 7 | 1 | READ response-header bytes | `11` |
| 8 | 1 | maximum READ data bytes | normally `244` |

Feature bits are STAT `0x0001`, LIST `0x0002`, READ `0x0004`, WRITE
`0x0008`, MKDIR `0x0010`, DELETE `0x0020`, and RENAME `0x0040`. A read-only
server omits all four mutation bits.

## 5. STAT

Request size is `2 + P`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x01` |
| 1 | 1 | path length `P` |
| 2 | `P` | relative path; `P = 0` means root |

Successful response size is 13 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x81` |
| 1 | 1 | status `0x00` |
| 2 | 1 | object type |
| 3 | 2 | POSIX permission bits, low 9 bits only |
| 5 | 4 | file size, or `0xffffffff` if too large to represent |
| 9 | 4 | modification time as Unix seconds; zero if unavailable |

Object types are unknown `0`, regular file `1`, directory `2`, and other `3`.
Directory and non-regular object sizes are reported as zero.

STAT `hello.txt`:

```text
01 09 68 65 6c 6c 6f 2e 74 78 74
```

## 6. LIST

Request size is `4 + P`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x02` |
| 1 | 2 | cursor; start with zero |
| 3 | 1 | directory path length `P` |
| 4 | `P` | directory path; `P = 0` means root |

List the exported root from the beginning:

```text
02 00 00 00
```

Successful response starts with a five-byte page header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x82` |
| 1 | 1 | status `0x00` |
| 2 | 2 | next cursor; `0xffff` means end |
| 4 | 1 | entry count |

The page then contains `count` entries:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | object type |
| 1 | 1 | name length `N` |
| 2 | `N` | entry name only, not the parent path |
| `2 + N` | 4 | regular-file size, zero for directories |

Entry size is `6 + N`. `.` and `..` are never returned. Continue with the
returned cursor and exactly the same directory path. Directory order is the
Linux directory order; if another process changes that directory during a
paged traversal, restart at cursor zero.

## 7. READ

Request size is `7 + P`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x03` |
| 1 | 4 | byte offset |
| 5 | 1 | maximum data bytes `M`; zero means packet maximum |
| 6 | 1 | path length `P` |
| 7 | `P` | file path |

Read `hello.txt` from offset zero with the largest possible chunk:

```text
03 00 00 00 00 00 09 68 65 6c 6c 6f 2e 74 78 74
```

Successful response:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x83` |
| 1 | 1 | status `0x00` |
| 2 | 4 | returned offset |
| 6 | 4 | total file size |
| 10 | 1 | data length `N` |
| 11 | `N` | raw file bytes |

`N` is at most 244 with a 255-byte packet. Continue at `offset + N` until it
equals total size. Reading exactly at EOF succeeds with `N = 0`; reading past
EOF is a bad request. Files larger than `0xffffffff` bytes return TOO_LARGE.

## 8. WRITE

Request size is `8 + P + N`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x04` |
| 1 | 4 | byte offset |
| 5 | 1 | flags |
| 6 | 1 | path length `P` |
| 7 | `P` | file path |
| `7 + P` | 1 | data length `N` |
| `8 + P` | `N` | raw bytes to write |

Flags are CREATE `0x01` and TRUNCATE `0x02`. They may be combined. Without
CREATE, the file must already exist. TRUNCATE is applied when the request opens
the file, so normally use it only on the first chunk at offset zero. `N` may be
zero, allowing creation or truncation without data. Since a request is at most
255 bytes, `N <= 247 - P`.

Successful response size is 7 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | response opcode `0x84` |
| 1 | 1 | status `0x00` |
| 2 | 4 | requested offset |
| 6 | 1 | bytes actually written |

Advance by the returned byte count. A short successful write is valid.

## 9. MKDIR and DELETE

Both requests use the STAT path layout: opcode, path length, and non-empty
path. MKDIR is `0x05`; DELETE is `0x06`. A successful response is only:

```text
[opcode | 80] 00
```

MKDIR creates one directory and does not create missing parents. DELETE
removes one file, symlink, or empty directory. The exported root cannot be
deleted.

## 10. RENAME

Request size is `3 + A + B`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | opcode `0x07` |
| 1 | 1 | old path length `A` |
| 2 | `A` | old path |
| `2 + A` | 1 | new path length `B` |
| `3 + A` | `B` | new path |

Both paths must remain beneath the same exported root. A successful response
is `87 00`.

## 11. Client and deployment notes

Use explicit byte reads and writes for integers; do not cast packets onto a C
struct. Keep the returned file size from the first READ and verify it stays
consistent while downloading. WRITE is not transactional: a disconnect can
leave a partially written file, just like interrupted POSIX writes.

The default configuration is:

```text
plugin 4 /opt/squid/lib/plugins/libfilesystem.so
```

Examples of server environment settings:

```sh
SQUID_FS_ROOT=/srv/retro-files
SQUID_FS_READ_ONLY=1
```

Filesystem access has the Unix identity and permissions of squid-server. Do
not export sensitive directories. The root restriction limits paths but does
not replace normal Unix ownership, permissions, backups, or access control.
