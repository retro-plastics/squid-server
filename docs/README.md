# squid-server — Implementation Reference

This document is intended for contributors and maintainers. It covers architecture decisions, module internals, the build system, and known limitations that are relevant when extending or porting the server.

---

## Table of contents

1. [Architecture overview](#architecture-overview)
2. [Module breakdown](#module-breakdown)
3. [Plugin API](#plugin-api)
4. [Configuration parser](#configuration-parser)
5. [Logging system](#logging-system)
6. [Daemonization](#daemonization)
7. [Signal handling](#signal-handling)
8. [Plugin loader](#plugin-loader)
9. [Plugin registry](#plugin-registry)
10. [Build system](#build-system)
11. [Test suite](#test-suite)
12. [Code quality notes](#code-quality-notes)
13. [Known limitations and future work](#known-limitations-and-future-work)

---

## Architecture overview

squid-server is a C11 single-process server with a fixed-size plugin registry. The process lifetime looks like this:

```
main()
  └── run_server_app()                    src/app/
        ├── parse_server_config()         src/config/
        ├── print_server_usage() [opt]
        └── run_server_runtime()          src/runtime/
              ├── daemonize_process()     [daemon mode only]
              ├── init_server_log()
              ├── install_signal_handlers()
              ├── load_configured_plugins()
              │     ├── parse_server_plugin_config_file()
              │     └── load_server_plugin() × N
              ├── sleep_until_stopped()   [signal loop]
              ├── unload_server_plugins()
              └── close_server_log()
```

All modules are compiled into a single static library `squid_server_core` and linked into the `squid-server` executable. Plugins live in separate shared libraries loaded at runtime via `dlopen`.

There is intentionally no threading. The runtime loop calls `sleep(1)` and wakes on signal delivery. Packet dispatch is not yet wired to a real network layer; the plugin lifecycle (load → start → [handle_packet] → stop → unload) is fully implemented, awaiting connection to the libsquid platform.

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

- Line-oriented parser using `fgets` into a 1024-byte buffer
- Trims leading and trailing whitespace before tokenising
- Tokenises with `strtok` (acceptable: single-threaded, startup only)
- Recognises two directives: `system_plugin <path>` and `plugin <port> <path>`
- Rejects unknown directives, duplicate ports, out-of-range ports, and paths exceeding 512 bytes
- Returns a descriptive `const char *` error message on the first parse failure
- Lines beginning with `#` (after trimming) are skipped as comments

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
- Passes token pointers directly to `parse_system_plugin_line()` or `parse_plugin_line()`
- On any error, closes the file and returns -1 with an error string

**Line length limit:** `fgets` uses a 1024-byte buffer. Lines longer than 1023 characters (excluding the newline) will be split across multiple `fgets` calls. The parser will see the continuation as a new malformed line and reject it. Config lines are expected to be well under this limit in practice.

**Port validation:** Port numbers are parsed with `strtol` and validated in the range 1–15 for user plugins. Port 0 is handled exclusively through the `system_plugin` directive. The check for `is_valid_assignable_server_port()` is the authoritative guard.

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

The runtime loop polls `keep_running` with `sleep(1)`:

```c
while (keep_running != 0) {
    sleep(1);
}
```

The `volatile sig_atomic_t` type guarantees that the flag is read atomically and that the compiler does not hoist the load out of the loop. An alternative would be `sigsuspend()` with an appropriate signal mask, which would wake immediately on signal delivery rather than waiting up to 1 second.

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
- Copies `opt/squid/etc/squid_server.conf` into the staging `etc/` directory
- Copies all public headers from `include/squid_server/` into the staging `include/squid_server/` directory
- Adds subdirectories: `lib`, `src`, `tests`

### `lib/CMakeLists.txt`

Fetches `libsquid` from GitHub via `FetchContent` at configure time using a pinned commit hash. The `libsquid` target is then available as `squid` for linking.

### `src/CMakeLists.txt`

Defines `squid_server_core` as an object/interface library target. Each subdirectory (`app`, `config`, `log`, `runtime`, `plugin`) appends its sources with `target_sources(squid_server_core ...)`. The final executable `squid-server` links `squid_server_core`, `squid` (public), and `dl` (private, for `dlopen`).

All compilation targets use `-Wall -Wextra -Wpedantic`.

### Plugin targets

Each plugin (`lib/echo/`, `lib/squidsys/`) is a `MODULE` or `SHARED` library with its `LIBRARY_OUTPUT_DIRECTORY` pointed at the staging plugin directory. This means CMake places the built `.so` files directly into `bin/opt/squid/lib/plugins/` without a manual copy step.

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

**`sleep(1)` polling**
The main loop polls `keep_running` every second. `sigsuspend()` with a blocked signal set would react instantly to a termination signal rather than waiting up to one second. The difference is negligible for a server that is stopped infrequently.

**`strtok` in config parser**
`strtok` modifies the buffer in place and is not reentrant. This is acceptable because config parsing is single-threaded and the buffer is local. If config parsing is ever moved to a thread, replace with `strtok_r`.

**Validation prototypes in `plugin_api.h`**
`is_valid_server_port()` and `is_valid_assignable_server_port()` are declared in the public plugin header but implemented in the server core. Plugin authors do not need these. They could be moved to an internal `server_port.h` header.

**`umask(0)` in daemon**
Setting `umask(0)` means the daemon creates files with permissions determined solely by the `open()` or `creat()` call, without a mask applied. This is the traditional recommendation but means files are created world-readable/writable unless the mode argument is explicit. A stricter alternative is `umask(027)`.

---

## Known limitations and future work

| Item | Notes |
|------|-------|
| No PID file | Classic daemons write `/var/run/squid-server.pid`; needed for `kill $(cat ...)` patterns and some init systems |
| No `SIGHUP` reload | Sending SIGHUP to reload config without restart is a conventional daemon feature |
| No `sd_notify` | `systemd` `Type=notify` requires calling `sd_notify(0, "READY=1")` after plugin load; currently `Type=simple` is required |
| No packet dispatch | The runtime loop does not yet connect to libsquid platform hooks; `handle_packet` is never called |
| No per-plugin error recovery | A plugin that crashes takes down the whole process; no sandboxing or isolation |
| `signal()` portability | Replace with `sigaction()` for strict POSIX compliance |
| `sleep(1)` latency | Replace with `sigsuspend()` for instant signal response |
