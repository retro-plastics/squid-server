/*
 * local_transport.c
 *
 * Unix domain socket (SOCK_STREAM) transport implementation.
 * Provides the five libsquid platform hooks — send_char,
 * recv_char, get_tick, malloc, and free — wired to the open
 * client fd.  recv_char uses MSG_DONTWAIT so snet_burst() never
 * blocks.  get_tick returns a wrapping 8-bit counter in 20 ms
 * units derived from CLOCK_MONOTONIC.
 *
 * NOTES:
 *  A module-level static int transport_fd holds the active fd
 *  because the libsquid platform_t hooks have no user-data
 *  pointer.  This restricts the process to one active transport
 *  at a time, which is sufficient for the current server model.
 *  _POSIX_C_SOURCE 200112L is required for clock_gettime().
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#define _POSIX_C_SOURCE 200112L

#include "transport/local/local_transport.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/*
 * File-scope fd used by the libsquid platform hooks.
 * Set by local_transport_activate() before snet_init() is called.
 * The libsquid platform API does not carry a user-data pointer, so a
 * module-level variable is the only option for passing state to the hooks.
 */
static int transport_fd = -1;

/* ---- libsquid platform hook implementations ---- */

static int local_send_char(uint8_t c)
{
    return (write(transport_fd, &c, 1) == 1) ? 0 : -1;
}

static int local_recv_char(void)
{
    uint8_t c;
    ssize_t n = recv(transport_fd, &c, 1, MSG_DONTWAIT);
    return (n == 1) ? (int)(unsigned int)c : -1;
}

static uint8_t local_get_tick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /*
     * 20 ms ticks (50 per second).  The result is cast to uint8_t so it
     * wraps naturally, which is what libsquid expects for timeout arithmetic.
     */
    return (uint8_t)((ts.tv_sec * 50UL + (unsigned long)ts.tv_nsec / 20000000UL) & 0xFFU);
}

static void *local_malloc(uint16_t n)
{
    return malloc((size_t)n);
}

static void local_free(void *p)
{
    free(p);
}

static const squid_platform_t local_platform = {
    local_send_char,
    local_recv_char,
    local_get_tick,
    local_malloc,
    local_free
};

/* ---- Internal helpers ---- */

static int create_unix_socket(void)
{
    return socket(AF_UNIX, SOCK_STREAM, 0);
}

static void fill_sockaddr(struct sockaddr_un *addr, const char *path)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    strncpy(addr->sun_path, path, sizeof(addr->sun_path) - 1U);
}

static void init_transport_fields(
    struct server_local_transport *transport,
    const char *socket_path
)
{
    memset(transport, 0, sizeof(*transport));
    transport->base.platform = local_platform;
    transport->listen_fd = -1;
    transport->client_fd = -1;
    strncpy(transport->socket_path, socket_path, sizeof(transport->socket_path) - 1U);
}

/* ---- Public API ---- */

int local_transport_listen(
    struct server_local_transport *transport,
    const char *socket_path
)
{
    struct sockaddr_un addr;
    int fd = -1;

    if ((transport == NULL) || (socket_path == NULL)) {
        return -1;
    }

    if (strlen(socket_path) >= local_transport_socket_path_max) {
        return -1;
    }

    init_transport_fields(transport, socket_path);

    fd = create_unix_socket();
    if (fd < 0) {
        return -1;
    }

    /* Remove any stale socket file so bind does not fail. */
    unlink(socket_path);

    fill_sockaddr(&addr, socket_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 1) != 0) {
        close(fd);
        unlink(socket_path);
        return -1;
    }

    transport->listen_fd = fd;
    return 0;
}

int local_transport_accept(struct server_local_transport *transport)
{
    int fd = -1;

    if ((transport == NULL) || (transport->listen_fd < 0)) {
        return -1;
    }

    fd = accept(transport->listen_fd, NULL, NULL);
    if (fd < 0) {
        return -1;
    }

    transport->client_fd = fd;
    return 0;
}

int local_transport_connect(
    struct server_local_transport *transport,
    const char *socket_path
)
{
    struct sockaddr_un addr;
    int fd = -1;

    if ((transport == NULL) || (socket_path == NULL)) {
        return -1;
    }

    if (strlen(socket_path) >= local_transport_socket_path_max) {
        return -1;
    }

    init_transport_fields(transport, socket_path);

    fd = create_unix_socket();
    if (fd < 0) {
        return -1;
    }

    fill_sockaddr(&addr, socket_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    transport->client_fd = fd;
    return 0;
}

void local_transport_activate(struct server_local_transport *transport)
{
    if (transport == NULL) {
        return;
    }

    transport_fd = transport->client_fd;
}

void local_transport_close(struct server_local_transport *transport)
{
    if (transport == NULL) {
        return;
    }

    if (transport->client_fd >= 0) {
        close(transport->client_fd);
        transport->client_fd = -1;
    }

    if (transport->listen_fd >= 0) {
        close(transport->listen_fd);
        transport->listen_fd = -1;
        unlink(transport->socket_path);
    }

    transport_fd = -1;
}
