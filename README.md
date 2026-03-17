# squid-server

A lightweight, plugin-based Linux server that hosts the [libsquid](https://github.com/retro-plastics/libsquid) protocol engine and dispatches squid packets to dynamically loaded plugins.

---

## Overview

squid-server bridges the squid protocol to your application code through a clean shared-library plugin API. It handles all process lifecycle concerns — daemonization, signal handling, logging, transport setup, and plugin loading — so each plugin can focus entirely on packet handling.

The server supports two startup modes:

| Mode | Flag | Use case |
|------|------|----------|
| Console | `--console` | Development, debugging, systemd `Type=simple` |
| Daemon | `--daemon` | Classic background daemon (double-fork) |

---

## Features

- **Plugin-based dispatch** — each of the 16 squid ports (0–15) can be wired to a separate shared library
- **System plugin** — port 0 is reserved for the built-in `squidsys` plugin; ports 1–15 are available for application plugins
- **Dual transport** — communicate over a Unix domain socket (local testing) or a POSIX serial port (hardware deployment), selected by configuration
- **Full packet dispatch** — the runtime loop drives the libsquid engine via `snet_burst()` and routes received packets to the registered plugin on each port
- **Dual logging** — stdout in console mode, syslog (`LOG_DAEMON`) in daemon mode
- **Clean shutdown** — `SIGTERM` and `SIGINT` trigger graceful plugin teardown
- **Staged build tree** — the build produces a deployment-ready layout under `bin/opt/squid/` for local development
- **Echo plugin** — a minimal reference plugin that reflects incoming packet payload back to the client

---

## Requirements

- Linux (tested on kernel 5.x and later)
- CMake 3.16 or later
- GCC or Clang with C11 support
- `libdl` (part of glibc, present on all standard Linux systems)
- Internet access during the first build (CMake fetches `libsquid` via FetchContent)

---

## Building

```sh
# configure (debug build)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# build server, plugins, and tests
cmake --build build
```

After a successful build the staged tree is ready at `bin/opt/squid/`:

```
bin/opt/squid/
├── bin/
│   └── squid-server              # server executable
├── etc/
│   ├── squid_server.conf         # default configuration
│   ├── squid_server_local.conf   # local Unix-socket transport (development)
│   └── squid_server_serial.conf  # serial port transport template
├── include/squid_server/
│   └── plugin_api.h              # public plugin API header
└── lib/plugins/
    ├── libecho.so                 # echo reference plugin
    └── libsquidsys.so             # system plugin (port 0)
```

---

## Running

### Console mode (development)

```sh
SQUID_SERVER_STAGE_ROOT=./bin \
  ./bin/opt/squid/bin/squid-server \
  --console \
  --config ./bin/opt/squid/etc/squid_server_local.conf
```

Console mode writes log lines to standard output:

```
[info] squid-server starting in console mode with 16 plugin slots and 2 loaded plugins
[info] waiting for client on /tmp/squid_server.sock
[info] client connected
[info] squid link established
```

### Serial mode

```sh
./bin/opt/squid/bin/squid-server \
  --console \
  --config ./bin/opt/squid/etc/squid_server_serial.conf
```

Edit `squid_server_serial.conf` to set the correct device and baud rate before running.

### Daemon mode

```sh
./bin/opt/squid/bin/squid-server \
  --daemon \
  --config /opt/squid/etc/squid_server.conf
```

Daemon mode detaches from the terminal and routes all log output to syslog under the identifier `squid-server`. Check messages with:

```sh
journalctl -t squid-server -f
# or
grep squid-server /var/log/syslog
```

### Command-line reference

```
squid-server [options]

Options:
  --console           run in foreground, log to stdout
  --daemon            detach and run as a background daemon
  --config <path>     path to configuration file
                      (default: /opt/squid/etc/squid_server.conf)
  --help, -h          show usage and exit
```

---

## Configuration file

The default configuration file is `/opt/squid/etc/squid_server.conf`. Override the path with `--config`.

### Syntax

```
# lines beginning with # are comments

# path to the shared library for port 0 (optional — defaults to /opt/squid/lib/plugins/libsquidsys.so)
system_plugin <path>

# attach a shared library to a port in the range 1-15
plugin <port> <path>

# serial transport — omit serial_device to use the local Unix-socket transport instead
serial_device <device>          # e.g. /dev/ttyS0 or /dev/ttyUSB0
serial_baud   <rate>            # 1200 2400 4800 9600 19200 38400 57600 115200  (default: 9600)
serial_databits <7|8>           # (default: 8)
serial_parity   <none|even|odd> # (default: none)
serial_stopbits <1|2>           # (default: 1)
serial_flow     <none|rtscts|xonxoff> # (default: none)
```

### Transport selection

The transport is chosen automatically based on whether `serial_device` is present in the configuration file:

| Configuration | Transport used |
|---------------|---------------|
| No `serial_device` line | Unix domain socket at `/tmp/squid_server.sock` (local transport) |
| `serial_device <path>` present | POSIX serial port at the given device path |

### Local transport example

```
# squid_server_local.conf

system_plugin /opt/squid/lib/plugins/libsquidsys.so
plugin 1 /opt/squid/lib/plugins/libecho.so
```

### Serial transport examples

**8N1, no flow control (most common):**
```
serial_device /dev/ttyUSB0
serial_baud   9600

system_plugin /opt/squid/lib/plugins/libsquidsys.so
plugin 1 /opt/squid/lib/plugins/libecho.so
```

**8N2 with RTS/CTS (retro hardware handshake):**
```
serial_device   /dev/ttyS0
serial_baud     9600
serial_stopbits 2
serial_flow     rtscts

system_plugin /opt/squid/lib/plugins/libsquidsys.so
plugin 1 /opt/squid/lib/plugins/libecho.so
```

**7E1 with XON/XOFF (older terminal-style devices):**
```
serial_device   /dev/ttyS0
serial_baud     2400
serial_databits 7
serial_parity   even
serial_flow     xonxoff

system_plugin /opt/squid/lib/plugins/libsquidsys.so
plugin 1 /opt/squid/lib/plugins/libecho.so
```

### Rules

- `system_plugin` may appear at most once; defaults to `libsquidsys.so` if omitted
- `plugin` lines set user plugins for ports 1–15
- Each serial directive may appear at most once
- `serial_databits` must be `7` or `8`; `serial_parity` must be `none`, `even`, or `odd`; `serial_stopbits` must be `1` or `2`; `serial_flow` must be `none`, `rtscts`, or `xonxoff`
- Duplicate port assignments are rejected at startup
- Paths longer than 512 characters are rejected; device paths longer than 64 characters are rejected
- Unknown directives cause startup to abort with an error

---

## Running under systemd

The recommended systemd mode is `--console`. The server responds to `SIGTERM`, so `systemctl stop` works cleanly. Log output is captured by `journald`.

Create `/etc/systemd/system/squid-server.service`:

```ini
[Unit]
Description=squid-server — libsquid protocol engine host
After=network.target

[Service]
Type=simple
ExecStart=/opt/squid/bin/squid-server --console --config /opt/squid/etc/squid_server.conf
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```

Enable and control the service:

```sh
sudo systemctl daemon-reload
sudo systemctl enable squid-server
sudo systemctl start  squid-server
sudo systemctl status squid-server
sudo systemctl stop   squid-server

# follow logs
journalctl -u squid-server -f
```

> **Note:** `--daemon` mode is not required under systemd and is not recommended for it. Use `--daemon` only when running squid-server outside of a service manager.

---

## Writing a plugin

Every plugin is a shared library that exports a single symbol:

```c
const struct server_plugin *get_server_plugin(void);
```

Include the public header in your plugin source:

```c
#include <squid_server/plugin_api.h>
```

### Minimal plugin skeleton

```c
#include <squid_server/plugin_api.h>
#include <string.h>

static int start_my_plugin(struct server_plugin *plugin)
{
    (void)plugin;
    return 0;   /* return non-zero to abort loading */
}

static int stop_my_plugin(struct server_plugin *plugin)
{
    (void)plugin;
    return 0;
}

static int handle_my_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    (void)plugin;
    (void)port_number;
    (void)request;

    /* write your response into response->packet_data,
       set response->packet_size, return 0 on success */
    response->packet_size = 0;
    return 0;
}

static struct server_plugin my_plugin = {
    .plugin_name    = "my_plugin",
    .plugin_context = NULL,
    .start          = start_my_plugin,
    .stop           = stop_my_plugin,
    .handle_packet  = handle_my_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &my_plugin;
}
```

Compile as a shared library and register the path in the configuration file:

```
plugin 2 /opt/squid/lib/plugins/libmyplugin.so
```

### Plugin API types

| Type | Purpose |
|------|---------|
| `struct server_plugin` | Plugin descriptor: name, context pointer, callbacks |
| `struct server_packet_view` | Read-only incoming packet (`packet_data`, `packet_size`) |
| `struct server_packet_buffer` | Write buffer for the response (`packet_data`, `packet_capacity`, `packet_size`) |
| `server_plugin_start_fn` | Called once after the plugin is loaded |
| `server_plugin_stop_fn` | Called once before the plugin is unloaded |
| `server_plugin_handle_packet_fn` | Called for every incoming packet on the registered port |

### Port constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `server_system_port` | 0 | Reserved for squidsys |
| `server_plugin_port_min` | 1 | Lowest user-assignable port |
| `server_plugin_port_max` | 15 | Highest user-assignable port |
| `server_port_count` | 16 | Total number of port slots |
| `server_plugin_api_version` | 1 | Current API version |

---

## Running tests

```sh
ctest --test-dir build --output-on-failure
```

---

## Known limitations

- No PID file — `/var/run/squid-server.pid` is not created; process tracking relies on the service manager or `pgrep`
- No `SIGHUP` reload — configuration changes require a restart
- No systemd `sd_notify` integration — `Type=notify` is not supported; use `Type=simple`
- One client at a time — the local transport accepts a single connection; the serial transport is point-to-point by nature
- No per-plugin error recovery — a plugin that crashes takes down the whole process

---

## Development path prefix

During local development, plugin paths in the configuration file are absolute (e.g. `/opt/squid/lib/plugins/libecho.so`). Set `SQUID_SERVER_STAGE_ROOT` to a local prefix and the loader prepends it to every absolute path:

```sh
export SQUID_SERVER_STAGE_ROOT=./bin
# resolves /opt/squid/lib/plugins/libecho.so
# to       ./bin/opt/squid/lib/plugins/libecho.so
```

This variable is intended for development only and should not be set in production.
