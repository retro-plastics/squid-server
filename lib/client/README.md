# Minimal squid plugin clients

This is the application-facing library for every bundled plugin, not just a
transport adapter. Include `squid_client/client.h` for everything, or include
one small plugin header when RAM and dependencies need to be obvious. The API
is allocation-free, reuses one caller-owned workspace, and is identical on
portable C and Z80.

The client never splits a request into 16-byte wire blocks. One `squid_send()`
queues the complete request and libsquid performs all wire segmentation and
reassembly. The library only adds the one-byte squid-server packet length and
waits until the complete response has arrived. This extra byte is necessary
because libsquid is a byte stream: a short `squid_recv()` is not a message
boundary.

## Smallest useful client

After initializing libsquid and opening a socket for the plugin channel:

```c
#include <squid_client/client.h>
#include <squid/socket.h>

static uint8_t workspace[17];             /* time reply is 16 bytes */
static squid_client_t client;

int fd = squid_open(sizeof(workspace), sizeof(workspace));
squid_connect(fd, 5);                    /* time plugin in the sample config */
squid_client_init(&client, fd, workspace, 16, 0, 0);
```

An interrupt-driven Z80 program can pass a null idle callback, as above. A
polling program supplies a callback that calls `snet_burst()` while a reply is
pending. A time request is then one call:

```c
squid_client_time_t now;
int status = squid_client_time_get(&client, SQUID_TIME_MODE_LOCAL, &now);
```

Return value zero means success. Positive values are the plugin's documented
status byte. Negative `SQUID_CLIENT_ERROR_*` values report a local argument,
link, I/O, protocol, overflow, or cancellation failure. Returned byte strings
point into `workspace` and remain valid until the next client call.

## Public plugin functions

Each header is usable on its own. `client.h` is only an umbrella which includes
all seven headers; it adds no code to the result.

| Header | Public calls |
|---|---|
| `base.h` | `squid_client_init`, low-level `squid_client_exchange` |
| `echo.h` | `squid_client_echo` |
| `system.h` | `squid_client_system_id` |
| `time.h` | `squid_client_time_get` |
| `filesystem.h` | `squid_client_fs_stat/list/list_next/read/write/mkdir/delete/rename` |
| `retrovault.h` | `squid_client_retro_list/search/list_next/info/info_next/download` |
| `tcp_proxy.h` | `squid_client_tcp_connect/write/read/close/status` |

List and info iterators decode variable records in place; clients do not need
to calculate offsets or parse plugin packets themselves.

For example, a Z80 filesystem client only needs:

```c
#include <squid_client/filesystem.h>

squid_client_fs_read_t chunk;
int status = squid_client_fs_read(&client, "games/demo.tap", 0, 128, &chunk);
if (status == 0) {
    /* chunk.data.data points directly into the client workspace. */
    consume(chunk.data.data, chunk.data.size);
}
```

And the TCP proxy is ordinary calls rather than hand-built packets:

```c
#include <squid_client/tcp_proxy.h>

uint8_t family, written;
squid_client_tcp_connect(&client, "example.com", 80, &family);
squid_client_tcp_write(&client, request, request_size, &written);
```

## Libraries

The normal CMake build creates the portable archive:

```text
bin/opt/squid/lib/client/portable/libsquid_client.a
bin/opt/squid/lib/client/portable/libsquid.a
```

Optional Docker/xcc targets create Partner and Spectrum archives and run the
Z80 checks:

```sh
cmake --build build --target squid-client-z80-partner
cmake --build build --target squid-client-z80-spectrum
# or build both
cmake --build build --target squid-client-z80
```

The results are:

```text
bin/opt/squid/lib/client/z80/partner/libsquid_client.a
bin/opt/squid/lib/client/z80/spectrum/libsquid_client.a
```

Each Z80 directory also contains the matching wire-v2 `libsquid.a`; both
archives have identical `.lib` spellings for toolchains that expect them. Link
`libsquid_client.a` before `libsquid.a`. Every plugin group is a separate
archive member, so the linker pulls only the calls the program uses. Every Z80
plugin serializer and parser is hand-written assembly under `z80/`; the C
implementations are used only for the portable archive.

Each Z80 build also writes `code-sizes.txt` beside the archive. It reports the
actual `_CODE` bytes for the core and each plugin archive member, making the
cost visible without guessing from C source or textual `.rel` file sizes.

The Partner target executes fragmented-response and typed-call scenarios with
xemu. The Spectrum image has no emulator platform, so its target performs a
native `zx-ram` link check; it uses the same tested assembly transaction engine.

## Spectrum 48K + Interface 1 at 115,200 baud

The Spectrum archive also provides the physical libsquid platform for the
Interface 1 serial connector. It is deliberately optional and remains in its
own archive members. Include `squid_client/spectrum_if1.h` and initialize a
channel in one call:

```c
#include <squid_client/spectrum_if1.h>

static uint8_t workspace[17];       /* enough for the 16-byte time reply */
static squid_client_t client;

int status = squid_client_spectrum_if1_open(
    &client, 5, workspace, 16);
```

The application must call `snet_burst()` from its 50 Hz interrupt. The serial
hooks preserve the prior interrupt state, so they also work when burst is
driven by foreground polling. Once `snet_link_is_up()` is true, the normal
typed calls can be used unchanged.

This backend is specifically for a 48K Spectrum with Interface 1 ports `0xEF`
and `0xF7`. Configure the Linux side for 115200 baud, 8 data bits, no parity,
2 stop bits, and RTS/CTS; `squid_server_spectrum.conf` is ready apart from its
device path. It is not the AY-port implementation used by later 128K models.
Libsquid's CRC, acknowledgements, and retransmission provide the error recovery
that the original 115,200-baud design expects at this marginal bit rate.

The cycle-counted bit-banging and 20-byte receive burst follow Tomaž Štih's
*The YX Kernel for ZX Spectrum* thesis, sections 9.3.1-9.3.7. The original
[send](https://github.com/retro-vault/yx/blob/master/os/ram/ssend115k.s) and
[receive](https://github.com/retro-vault/yx/blob/master/os/ram/srecv115k.s) sources
are available in the YX repository.
