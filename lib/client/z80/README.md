# Z80 plugin library

`libsquid_client.a` is a normal Z80 C library with public functions for every
squid-server plugin. Applications do not construct protocol packets or manage
negotiated-size wire blocks.

Use the umbrella header:

```c
#include <squid_client/client.h>
```

Or include only the plugin being used:

```c
#include <squid_client/time.h>

squid_client_time_t now;
int status = squid_client_time_get(&client, SQUID_TIME_MODE_LOCAL, &now);
```

The individual public headers are `echo.h`, `system.h`, `time.h`,
`filesystem.h`, `retrovault.h`, and `tcp_proxy.h`. Every corresponding Z80
implementation is hand-written assembly in this directory:

| Source | Protocol implementation |
|---|---|
| `echo.s` | Echo request and zero-copy reply |
| `system.s` | System identification request |
| `time.s` | Time request and fixed-record decoder |
| `filesystem.s` | Stat, list iterator, read, write, mkdir, delete, rename |
| `retrovault.s` | List, search, iterators, info, download |
| `tcp_proxy.s` | Connect, write, read, status, close |
| `client.s` | Synchronous packet transaction engine |
| `internal.s` | Shared response, endian, string, and copy helpers |

No plugin protocol C source is compiled into the Z80 archive. The C sources in
the parent directory belong to the portable library only. Public and internal
headers contain declarations and data types only.

Every plugin is a separate archive member. Linking a time-only application,
for example, does not pull the filesystem, Retro Vault, or TCP proxy client.
`code-sizes.txt`, generated beside each Z80 archive, lists the actual code size
of every assembly member. The emulator also runs the complete portable service
fixture suite against these Z80 implementations, covering every public call.
