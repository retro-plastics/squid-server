/*
 * local_transport.h
 *
 * Unix domain socket transport for local (same-machine) testing.
 * The server side calls listen/accept; the client side calls
 * connect.  Both sides call activate() before snet_init() so
 * that the libsquid platform hooks are pointed at the live fd.
 *
 * NOTES:
 *  Only one client at a time is supported (backlog = 1).
 *  The socket path is limited to 108 bytes (POSIX sun_path).
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_LOCAL_TRANSPORT_H
#define SERVER_LOCAL_TRANSPORT_H

#include "transport/server_transport.h"

/* POSIX limit for Unix domain socket paths. */
#define local_transport_socket_path_max 108

/*
 * Default socket path used by both the server and the sample client programs.
 * Override at runtime by passing a different path to the init functions.
 */
#define local_transport_default_socket_path "/tmp/squid_server.sock"

/*
 * Local (Unix domain socket) transport state.
 *
 * Server side:  listen_fd holds the listening socket; client_fd is filled
 *               by local_transport_accept() when a client connects.
 *
 * Client side:  listen_fd is -1; client_fd is filled by
 *               local_transport_connect().
 *
 * Call local_transport_activate() once the client_fd is valid and before
 * calling snet_init(), so that the libsquid platform hooks read from and
 * write to the correct file descriptor.
 */
struct server_local_transport {
    struct server_transport base;                       /* must be first */
    int listen_fd;
    int client_fd;
    char socket_path[local_transport_socket_path_max];
};

/*
 * Server side: create a Unix domain socket at socket_path, bind it, and
 * start listening.  Removes any stale socket file first.
 * Returns 0 on success, -1 on error (errno is set).
 */
int local_transport_listen(
    struct server_local_transport *transport,
    const char *socket_path
);

/*
 * Server side: block until a client connects and fill transport->client_fd.
 * Returns 0 on success, -1 on error.
 */
int local_transport_accept(struct server_local_transport *transport);

/*
 * Client side: connect to a server that is listening at socket_path.
 * Fills transport->client_fd on success.
 * Returns 0 on success, -1 on error (errno is set).
 */
int local_transport_connect(
    struct server_local_transport *transport,
    const char *socket_path
);

/*
 * Point the file-scope libsquid platform hooks at transport->client_fd.
 * Must be called once, after client_fd is valid, before snet_init().
 */
void local_transport_activate(struct server_local_transport *transport);

/*
 * Close all open file descriptors.  On the server side, also unlinks the
 * socket file so the path can be reused on the next run.
 */
void local_transport_close(struct server_local_transport *transport);

#endif
