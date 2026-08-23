/*
 * server_runtime.h
 *
 * Declaration of the server_runtime aggregate and the two
 * functions that govern its lifetime: init_server_runtime()
 * sets up defaults and run_server_runtime() drives the entire
 * server process from plugin load through packet dispatch to
 * graceful shutdown.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_RUNTIME_H
#define SERVER_RUNTIME_H

#include "config/server_config.h"
#include "config/server_plugin_config.h"
#include "plugin/server_plugin_loader.h"
#include "plugin/server_plugin_registry.h"
#include "transport/local/local_transport.h"
#include "transport/serial/serial_transport.h"

#include "squid_server/plugin_api.h"

/*
 * libsquid uses caller-owned rings with 8-bit indices.  A ring may
 * contain at most 255 bytes and reserves one byte to distinguish full from
 * empty, leaving 254 bytes of usable capacity per socket direction.
 */
#define server_squid_ring_size 255U

/* One byte in every socket message carries the packet length. */
#define server_packet_max (server_squid_ring_size - 2U)

/*
 * All state for one server run.  Every subsystem is embedded by
 * value so that a single stack allocation in main() holds the
 * entire server.
 */
struct server_runtime {
    struct server_config             config;
    struct server_plugin_config_file plugin_config;
    struct server_plugin_registry    plugin_registry;
    struct server_plugin_loader      plugin_loader;
    struct server_local_transport    transport;
    struct server_serial_transport   serial_transport;
    /*
     * One libsquid socket fd per port slot (0-15).
     * Slots for unregistered ports hold -1.
     * Wire channel N maps to server port N (ports 1-15).
     * Port 0 is not exposed on the wire.
     */
    int squid_fds[server_port_count];
    uint8_t squid_tx_rings[server_port_count][server_squid_ring_size];
    uint8_t squid_rx_rings[server_port_count][server_squid_ring_size];
    uint8_t request_packets[server_port_count][server_packet_max];
    size_t request_packet_sizes[server_port_count];
    size_t request_packet_expected[server_port_count];
    uint8_t pending_responses[server_port_count][server_squid_ring_size - 1U];
    size_t pending_response_sizes[server_port_count];
};

/* Zero-initialise runtime and copy config into it. */
void init_server_runtime(
    struct server_runtime *runtime,
    const struct server_config *config
);

/*
 * Run the server: load plugins, accept one client, exchange
 * packets until the link is lost or a signal arrives.
 * Returns 0 on clean exit, 1 on unrecoverable error.
 */
int run_server_runtime(struct server_runtime *runtime);

#endif
