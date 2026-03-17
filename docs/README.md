# squid-server — Implementation Reference

This document is intended for contributors and maintainers. It covers architecture decisions, module internals, the build system, and known limitations that are relevant when extending or porting the server.

---

## Table of contents

1. [Architecture overview](#architecture-overview)
2. [Module breakdown](#module-breakdown)
3. [Plugin API](#plugin-api)
4. [Configuration parser](#configuration-parser)
5. [Transport layer](#transport-layer)
6. [Logging system](#logging-system)
7. [Daemonization](#daemonization)
8. [Signal handling](#signal-handling)
9. [Plugin loader](#plugin-loader)
10. [Plugin registry](#plugin-registry)
11. [Build system](#build-system)
12. [Test suite](#test-suite)
13. [Code quality notes](#code-quality-notes)
14. [Known limitations and future work](#known-limitations-and-future-work)

---

## Architecture overview

squid-server is a C11 single-process server with a fixed-size plugin registry. The process lifetime looks like this:

```
main()
  └── run_server_app()                       src/app/
        ├── parse_server_config()            src/config/
        ├── print_server_usage() [opt]
        └── run_server_runtime()             src/runtime/
              ├── daemonize_process()        [daemon mode only]
              ├── init_server_log()
              ├── install_signal_handlers()
              ├── load_configured_plugins()
              │     ├── parse_server_plugin_config_file()
              │     └── load_server_plugin() × N
              ├── [transport setup]
              │     ├── serial: serial_transport_open/activate
              │     └── local:  local_transport_listen/accept/activate
              ├── snet_init()                libsquid protocol engine
              ├── open_plugin_sockets()      squid_open/squid_bind × N
              ├── run_dispatch_loop()
              │     ├── [wait for squid link handshake]
              │     └── snet_burst() + dispatch_received_packets() × N
              ├── close_plugin_sockets()
              ├── [transport]_transport_close()
              ├── unload_server_plugins()
              └── close_server_log()
```

All modules are compiled into a single static library `squid_server_core` and linked into the `squid-server` executable. Plugins live in separate shared libraries loaded at runtime via `dlopen`.

There is intentionally no threading. The dispatch loop calls `snet_burst()` and `usleep(5000)` (5 ms) per iteration, well within one 20 ms libsquid tick period. The loop exits when `snet_link_is_up()` returns false (peer restart or max retransmit exceeded) or a termination signal arrives.

---

## Module breakdown

### `src/main.c`

Trivial entry point. Delegates entirely to `run_server_app()`. Kept minimal so that the executable target has no logic of its own.

### `src/app/` — application startup

`server_app.c` is the top-level orchestrator:

1. Calls `parse_server_config()` to process `argv`
2. Prints usage and exits 0 if `show_help` is set
3. Calls `init_server_runtime()` and `run_server_runtime()`
4. Returns the runtime exit code

### `src/config/` — argument and file parsing

Two distinct responsibilities kept in separate files:

**`server_config.c`** — command-line arguments

- Recognises `--console`, `--daemon`, `--config <path>`, `--help`, `-h`
- Validates `--config` path length (max 512 bytes, `server_config_path_max`)
- Returns an error string via `const char **error_message` on failure
- Default run mode: `server_run_mode_console`
- Default config path: `/opt/squid/etc/squid_server.conf`

**`server_plugin_config.c`** — configuration file

Parsed result is stored in `server_plugin_config_file`, which carries plugin mappings and the full set of serial line parameters (`serial_device`, `serial_baud`, `serial_databits`, `serial_parity`, `serial_stopbits`, `serial_flow`).

- Line-oriented parser using `fgets` into a 1024-byte buffer
- Trims leading and trailing whitespace before tokenising
- Tokenises with `strtok` (acceptable: single-threaded, startup only)
- Recognises eight directives: `system_plugin`, `plugin`, `serial_device`, `serial_baud`, `serial_databits`, `serial_parity`, `serial_stopbits`, `serial_flow`
- Rejects unknown directives, duplicate ports, out-of-range ports, paths exceeding 512 bytes, device paths exceeding 64 bytes, and invalid serial parameter values
- Returns a descriptive `const char *` error message on the first parse failure
- Lines beginning with `#` (after trimming) are skipped as comments
- Serial defaults: `serial_device` empty (local transport), `serial_baud` 9600, `serial_databits` 8, `serial_parity` none, `serial_stopbits` 1, `serial_flow` none

### `src/transport/` — transport back-ends

See [Transport layer](#transport-layer).

### `src/log/` — logging

See [Logging system](#logging-system).

### `src/runtime/` — process lifetime

See [Daemonization](#daemonization) and [Signal handling](#signal-handling).

### `src/plugin/` — plugin registry and loader

See [Plugin registry](#plugin-registry) and [Plugin loader](#plugin-loader).

### `include/squid_server/plugin_api.h` — public API

The only header that plugin authors need. See [Plugin API](#plugin-api).

---

## Plugin API

`include/squid_server/plugin_api.h` defines the complete contract between the server and its plugins.

### Port constants

```c
#define server_system_port       0   /* reserved for squidsys */
#define server_plugin_port_min   1   /* first user port */
#define server_plugin_port_max  15   /* last user port */
#define server_port_min          0   /* alias for server_system_port */
#define server_port_max         15   /* alias for server_plugin_port_max */
#define server_port_count       16   /* total slots */
#define server_plugin_api_version 1
```

### Data structures

```c
struct server_packet_view {
    const void *packet_data;   /* pointer to incoming packet bytes (read-only) */
    size_t      packet_size;   /* number of valid bytes at packet_data */
};

struct server_packet_buffer {
    void   *packet_data;       /* write response bytes here */
    size_t  packet_capacity;   /* maximum bytes available at packet_data */
    size_t  packet_size;       /* set this to the number of bytes written */
};

struct server_plugin {
    const char                    *plugin_name;    /* human-readable identifier */
    void                          *plugin_context; /* opaque per-plugin state */
    server_plugin_start_fn         start;
    server_plugin_stop_fn          stop;
    server_plugin_handle_packet_fn handle_packet;
};
```

### Callbacks

| Callback | Signature | Called when | Return |
|----------|-----------|-------------|--------|
| `start` | `int(struct server_plugin *)` | After registration, before any packet | 0 = ok, non-zero = abort load |
| `stop` | `int(struct server_plugin *)` | Before dlclose, during shutdown | ignored |
| `handle_packet` | `int(plugin, port, request, response)` | For each incoming packet | 0 = ok, non-zero = error |

All callbacks may be NULL. NULL `start` and `stop` are skipped silently.

### Exported symbol

Every plugin shared library must export:

```c
const struct server_plugin *get_server_plugin(void);
```

The server resolves this symbol with `dlsym` after `dlopen`. The returned pointer must remain valid for the lifetime of the plugin. In practice, plugins return a pointer to a file-scope `static struct server_plugin`.

### Design note — validation prototypes in the API header

`plugin_api.h` also declares `is_valid_server_port()` and `is_valid_assignable_server_port()`. These are implemented in `src/plugin/server_plugin_registry.c` and are server-side utilities. Plugin authors do not need them; the declarations are a documentation artefact. A future cleanup could move them to an internal header.

---

## Configuration parser

The configuration parser (`src/config/server_plugin_config.c`) is intentionally simple and stateless:

- Opens the file with `fopen`, reads line by line with `fgets`
- Tokenises each non-comment, non-empty line with `strtok`
- Dispatches on the keyword token to one of: `parse_system_plugin_line()`, `parse_plugin_line()`, or inline handlers for `serial_device` and `serial_baud`
- On any error, closes the file and returns -1 with an error string

**Line length limit:** `fgets` uses a 1024-byte buffer. Lines longer than 1023 characters will be split; the parser will see the continuation as a malformed line and reject it. Config lines are expected to be well under this limit.

**Port validation:** Port numbers are parsed with `strtol` and validated in the range 1–15 for user plugins. Port 0 is handled exclusively through the `system_plugin` directive.

**Serial device default:** When neither `serial_device` nor `serial_baud` appears in the file, the struct is zero-initialised for `serial_device` (empty string → local transport used) and `serial_baud` defaults to 9600.

---

## Transport layer

The transport layer lives in `src/transport/` and is structured around a common base type:

```c
/* src/transport/server_transport.h */
struct server_transport {
    squid_platform_t platform;   /* libsquid I/O hooks — must be first */
};
```

Every concrete transport struct embeds `struct server_transport` as its first member, allowing safe pointer casts (C99 §6.7.2.1). The embedded `squid_platform_t` is passed directly to `snet_init()` once the connection is established.

Both transports are compiled into separate static libraries (`squid_transport_local` and `squid_transport_serial`) and linked into `squid_server_core`.

### Local transport (`src/transport/local/`)

Unix domain socket (SOCK_STREAM) transport for same-machine testing.

**API:**

| Function | Role |
|----------|------|
| `local_transport_listen(transport, path)` | Create, bind, and listen on a Unix socket. Removes any stale socket file first. |
| `local_transport_accept(transport)` | Block until a client connects; fills `client_fd`. |
| `local_transport_connect(transport, path)` | Client-side connect (used by sample programs). |
| `local_transport_activate(transport)` | Set the module-level `transport_fd` to `client_fd`. Call before `snet_init()`. |
| `local_transport_close(transport)` | Close all fds; unlink the socket file on the server side. |

`recv_char` uses `MSG_DONTWAIT` so `snet_burst()` never blocks.

### Serial transport (`src/transport/serial/`)

POSIX TTY transport for hardware deployment over RS-232, USB-serial adapters, or any character device.

**API:**

| Function | Role |
|----------|------|
| `serial_transport_open(transport, device, baud)` | Open the device with `O_RDWR | O_NOCTTY | O_NONBLOCK`, save original `termios`, configure raw 8N1. |
| `serial_transport_activate(transport)` | Set the module-level `transport_fd` to `fd`. Call before `snet_init()`. |
| `serial_transport_close(transport)` | Restore original `termios` settings and close the fd. |

All line parameters are passed via `serial_transport_config`. Terminal configuration applied by `configure_port()`:

- Baud rate: mapped from integer to `speed_t` constant via `cfsetispeed`/`cfsetospeed`
- Data bits: `CS7` or `CS8`; `CREAD | CLOCAL` always set (receiver enabled, ignore modem lines)
- Parity: `PARENB` set for even/odd; `PARODD` additionally set for odd; both cleared for none
- Stop bits: `CSTOPB` set for 2 stop bits; cleared for 1
- Flow control: `CRTSCTS` set for RTS/CTS; `IXON | IXOFF` set for XON/XOFF; all cleared for none
- `ICANON`, `ECHO`, `ISIG` cleared — raw input
- `OPOST`, `ONLCR` cleared — raw output
- `VMIN=0`, `VTIME=0` — non-blocking: return immediately with whatever is in the buffer

The original `termios` state is stored in `transport->saved_tty` and restored on `serial_transport_close()`.

### Transport selection in the runtime

After `load_configured_plugins()`, `run_server_runtime()` inspects `plugin_config.serial_device`:

```
serial_device is non-empty  →  build serial_transport_config from all serial_* fields
                               serial_transport_open / activate
serial_device is empty      →  local_transport_listen / accept / activate
```

Both paths call `snet_init(&transport->base.platform, &squid_timing)` with the same timing constants, so the libsquid engine behaves identically regardless of the underlying physical link.

### Module-level fd

Both transports use a file-scope `static int transport_fd` to pass the active fd to the platform hook callbacks. This restricts the process to one active transport at a time — sufficient for the current single-connection server model. The libsquid `squid_platform_t` struct carries no user-data pointer, so this is the only viable approach without changing the platform API.

---

## Logging system

`src/log/server_log.c` maintains a single file-scope state variable:

```c
static enum server_run_mode current_run_mode = server_run_mode_console;
```

**Console mode** (`server_run_mode_console`):

```
[info] message text
[warning] message text
[error] message text: strerror output
```

Output goes to `stdout` and is flushed after every write. This ensures log lines appear immediately when running interactively or under systemd, which captures stdout.

**Daemon mode** (`server_run_mode_daemon`):

`openlog("squid-server", LOG_PID | LOG_NDELAY, LOG_DAEMON)` is called from `init_server_log()`. Messages are sent with `syslog(priority, "%s", text)`. The `%s` format argument prevents format-string injection from message content.

Level mapping:

| `server_log_level` | syslog priority |
|--------------------|----------------|
| `server_log_level_info` | `LOG_INFO` |
| `server_log_level_warning` | `LOG_WARNING` |
| `server_log_level_error` | `LOG_ERR` |

`write_server_log_errno()` appends `: strerror(error_number)` to the message before dispatching through the normal path. It handles a NULL or empty `strerror` return by substituting `"unknown error"`.

---

## Daemonization

`daemonize_process()` in `src/runtime/server_runtime.c` implements the classic POSIX double-fork pattern:

```
parent process
  │
  ├── fork() #1
  │     ├── parent: daemonize_process() returns 0 → run_server_runtime() returns 0 → main() exits
  │     └── child #1 (session leader candidate):
  │           ├── setsid()        — create new session, drop controlling terminal
  │           ├── fork() #2
  │           │     ├── child #1: _exit(0)   — prevents the daemon from reacquiring a terminal
  │           │     └── child #2 (daemon):
  │           │           ├── chdir("/")      — release working directory
  │           │           ├── umask(0)        — let plugins/logs control their own permissions
  │           │           ├── freopen /dev/null → stdin
  │           │           ├── freopen /dev/null → stdout
  │           │           ├── freopen /dev/null → stderr
  │           │           └── returns 1       — signals "I am the daemon"
  │           └── (unreachable after _exit)
  └── (exits)
```

`run_server_runtime()` inspects the return value of `daemonize_process()`:

| Return | Meaning | Action |
|--------|---------|--------|
| `-1` | `fork()` or `setsid()` failed | log errno, return 1 |
| `0` | original parent | return 0 (process exits cleanly) |
| `1` | final daemon child | continue with log init and plugin loading |

`_exit(0)` is used for the intermediate child to avoid running `atexit` handlers and flushing stdio buffers a second time.

**`umask(0)`** is set so that the daemon does not inherit a restrictive umask from the invoking shell. Plugins or future file-creation code can set their own permissions explicitly.

**`chdir("/")`** prevents the daemon from holding a reference to any mountpoint.

---

## Signal handling

`install_signal_handlers()` registers handlers for `SIGINT` and `SIGTERM`:

```c
static volatile sig_atomic_t keep_running = 1;

static void handle_termination_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}
```

`signal()` is used rather than `sigaction()`. On Linux with glibc, `signal()` uses BSD semantics (signal is not reset to `SIG_DFL` after delivery, and slow syscalls are restarted). This is correct behaviour for this use case. A future improvement would be `sigaction()` with `SA_RESTART` for strict POSIX portability.

The dispatch loop checks `keep_running` at the top of each iteration alongside `snet_link_is_up()`:

```c
while (keep_running && snet_link_is_up()) {
    snet_burst();
    dispatch_received_packets(runtime);
    usleep(5000);
}
```

`usleep(5000)` (5 ms) keeps the loop well within one 20 ms libsquid tick period while avoiding busy-spin. An alternative would be `sigsuspend()`, but that does not suit a polling protocol engine.

The `volatile sig_atomic_t` type guarantees the flag is read atomically and that the compiler does not hoist the load out of the loop.

---

## Plugin loader

`src/plugin/server_plugin_loader.c` manages dynamic library loading and teardown.

### Loading sequence

For each plugin entry in the configuration:

1. Resolve the path — if `SQUID_SERVER_STAGE_ROOT` is set and the path is absolute, prepend the root
2. `dlopen(resolved_path, RTLD_NOW | RTLD_LOCAL)`
   - `RTLD_NOW` resolves all symbols at load time (fails fast on missing symbols)
   - `RTLD_LOCAL` prevents plugin symbols from leaking into the global namespace
3. Clear `dlerror()`, then call `dlsym(handle, "get_server_plugin")`; check `dlerror()` after
4. Call the factory function; validate the returned `server_plugin` pointer
5. Register the plugin in the registry
6. Call `plugin->start()` if non-NULL; on failure, unregister and `dlclose`
7. Store `{port, handle, plugin, path}` in the loader's flat array

### Teardown sequence

`unload_server_plugins()` iterates the loaded array in reverse-index order (last loaded, first unloaded):

1. Call `plugin->stop()` if non-NULL
2. Clear the registry slot
3. `dlclose(library_handle)`
4. Zero the slot

Reverse-order unloading is not strictly required here (plugins do not depend on each other), but it is a sound convention.

### `SQUID_SERVER_STAGE_ROOT`

This environment variable exists solely for local development. Production deployments use absolute paths in the config file and do not set this variable. When set, the loader prepends it to any absolute plugin path, effectively rerooting the installation tree.

```c
stage_root = getenv("SQUID_SERVER_STAGE_ROOT");
if ((stage_root != NULL) && (plugin_path[0] == '/')) {
    snprintf(resolved, sizeof(resolved), "%s%s", stage_root, plugin_path);
}
```

---

## Plugin registry

`src/plugin/server_plugin_registry.c` maintains a fixed 16-slot array indexed by port number:

```c
struct server_plugin_registry {
    struct server_plugin_slot slots[server_port_count];
};
```

Index calculation: `slot_index = port_number - server_port_min`. Since `server_port_min == 0`, the index equals the port number directly.

Operations:

| Function | Behaviour |
|----------|-----------|
| `init_server_plugin_registry()` | `memset` to zero |
| `register_server_plugin(reg, port, plugin)` | Validates port; rejects if slot is occupied; stores pointer |
| `find_server_plugin(reg, port)` | Returns stored pointer or NULL |
| `count_registered_server_plugins(reg)` | Linear scan counting non-NULL slots |
| `is_valid_server_port(port)` | Returns non-zero if `port <= server_port_max` |
| `is_valid_assignable_server_port(port)` | Returns non-zero if `port >= server_plugin_port_min && port <= server_plugin_port_max` |

The registry holds `const struct server_plugin *` pointers. The plugin structs are owned by their shared libraries and remain valid until `dlclose`.

---

## Build system

### Root `CMakeLists.txt`

- Minimum CMake 3.16, C11 with `CMAKE_C_EXTENSIONS OFF` (no GNU extensions)
- Staging root: `${CMAKE_SOURCE_DIR}/bin/opt/squid/`
- Creates the staging directory tree with `file(MAKE_DIRECTORY ...)`
- Copies `opt/squid/etc/squid_server.conf`, `squid_server_local.conf`, and `squid_server_serial.conf` into the staging `etc/` directory — `opt/squid/etc/` is the canonical source for all configuration templates
- Copies all public headers from `include/squid_server/` into the staging `include/squid_server/` directory
- Adds subdirectories: `lib`, `src`, `tests`, `samples`

### `lib/CMakeLists.txt`

Fetches `libsquid` from GitHub via `FetchContent` at configure time using a pinned commit hash. The `libsquid` target is then available as `squid` for linking.

### `src/CMakeLists.txt`

Defines `squid_server_core` as a static library target. Each subdirectory appends sources with `target_sources`. The final executable `squid-server` links `squid_server_core`, `squid` (public), and `dl` (private, for `dlopen`).

### `src/transport/CMakeLists.txt`

Adds `local` and `serial` subdirectories. Each builds a static library (`squid_transport_local`, `squid_transport_serial`) and links it into `squid_server_core` with `target_link_libraries(squid_server_core PUBLIC ...)`.

All compilation targets use `-Wall -Wextra -Wpedantic`.

### Plugin targets

Each plugin (`lib/echo/`, `lib/squidsys/`) is a `SHARED` library with its `LIBRARY_OUTPUT_DIRECTORY` pointed at the staging plugin directory. CMake places the built `.so` files directly into `bin/opt/squid/lib/plugins/` without a manual copy step.

### Useful CMake variables

| Variable | Effect |
|----------|--------|
| `CMAKE_BUILD_TYPE=Debug` | Enable debug info, disable optimisation |
| `CMAKE_BUILD_TYPE=Release` | Enable optimisation, strip debug info |

---

## Test suite

`tests/test_main.c` contains six tests run as a single CTest entry `squid_server_tests`:

| Test | What it verifies |
|------|-----------------|
| `test_parse_defaults` | No-argument parse yields console mode, `show_help=false`, default config path |
| `test_parse_daemon_flag` | `--daemon` sets `server_run_mode_daemon` |
| `test_parse_config_flag` | `--config /tmp/test.conf` stores the path |
| `test_parse_unknown_flag` | Unknown argument returns non-zero and sets `"unknown argument"` error string |
| `test_plugin_registry` | Register two plugins, reject duplicate, find by port, reject out-of-range port, count |
| `test_plugin_config_file` | Write a temp config file, parse it, verify system plugin path and one port mapping |

The test executable is self-contained: it writes to stderr on failure and returns 1; it prints `"all tests passed"` and returns 0 on success.

Run with:

```sh
ctest --test-dir build --output-on-failure
```

---

## Code quality notes

The following observations are informational — not bugs, but worth noting for future work:

**`signal()` vs `sigaction()`**
`install_signal_handlers()` uses `signal()`. On Linux with glibc, `signal()` uses BSD semantics, which is correct here. For strict POSIX portability and explicit control over `SA_RESTART`, `sigaction()` with `sa_flags = SA_RESTART` is preferred.

**`usleep(5000)` polling**
The dispatch loop polls with a fixed 5 ms sleep between iterations. This is adequate for the libsquid 20 ms tick period and avoids busy-spin, but does not react to link loss or signal delivery faster than 5 ms. An event-driven approach (e.g. `select()` or `poll()` on the transport fd) would reduce latency.

**`strtok` in config parser**
`strtok` modifies the buffer in place and is not reentrant. This is acceptable because config parsing is single-threaded and the buffer is local. If config parsing is ever moved to a thread, replace with `strtok_r`.

**Validation prototypes in `plugin_api.h`**
`is_valid_server_port()` and `is_valid_assignable_server_port()` are declared in the public plugin header but implemented in the server core. Plugin authors do not need these. They could be moved to an internal `server_port.h` header.

**`umask(0)` in daemon**
Setting `umask(0)` means the daemon creates files with permissions determined solely by the `open()` or `creat()` call, without a mask applied. This is the traditional recommendation but means files are created world-readable/writable unless the mode argument is explicit. A stricter alternative is `umask(027)`.

**Module-level fd in transports**
Both transports use a `static int transport_fd` because the libsquid `squid_platform_t` hook callbacks carry no user-data pointer. This limits the process to one active transport, which is acceptable for the current single-connection model. If multi-connection support is ever needed, the platform API would need a user-data extension.

---

## Known limitations and future work

| Item | Notes |
|------|-------|
| No PID file | Classic daemons write `/var/run/squid-server.pid`; needed for `kill $(cat ...)` patterns and some init systems |
| No `SIGHUP` reload | Sending SIGHUP to reload config without restart is a conventional daemon feature |
| No `sd_notify` | systemd `Type=notify` requires calling `sd_notify(0, "READY=1")` after plugin load; currently `Type=simple` is required |
| One client at a time | Both transports support a single connection; the local transport backlog is 1 |
| No per-plugin error recovery | A plugin that crashes takes down the whole process; no sandboxing or isolation |
| `signal()` portability | Replace with `sigaction()` for strict POSIX compliance |
| Fixed poll interval | Replace `usleep(5000)` with `poll()`/`select()` on the transport fd for event-driven dispatch |
