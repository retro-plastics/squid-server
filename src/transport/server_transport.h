/*
 * server_transport.h
 *
 * Abstract base type for all transport back-ends.  Every concrete
 * transport struct (e.g. server_local_transport) embeds this as
 * its first member so that a pointer to any transport can be
 * safely cast to server_transport* and back again (C99 §6.7.2.1).
 *
 * The embedded squid_platform_t is passed directly to snet_init()
 * once the physical connection is established.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_TRANSPORT_H
#define SERVER_TRANSPORT_H

#include <squid/snet.h>

/*
 * Common transport header — must be the first member of every
 * concrete transport struct.
 */
struct server_transport {
    squid_platform_t platform; /* libsquid I/O hooks — must be first */
};

#endif
