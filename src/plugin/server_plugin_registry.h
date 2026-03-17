/*
 * server_plugin_registry.h
 *
 * Types and interface for the fixed-size plugin registry that
 * stores one plugin pointer per server port slot (0-15).
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_PLUGIN_REGISTRY_H
#define SERVER_PLUGIN_REGISTRY_H

#include "squid_server/plugin_api.h"

#include <stddef.h>

/* One slot in the registry — port number plus plugin pointer. */
struct server_plugin_slot {
    uint8_t port_number;
    const struct server_plugin *plugin; /* NULL when empty */
};

/* Registry covering all ports 0-15. */
struct server_plugin_registry {
    struct server_plugin_slot slots[server_port_count];
};

/* Initialise all slots to empty (plugin = NULL). */
void init_server_plugin_registry(
    struct server_plugin_registry *registry
);

/*
 * Register plugin at port_number.
 * Returns -1 if the slot is already occupied or the port is
 * out of range.
 */
int register_server_plugin(
    struct server_plugin_registry *registry,
    uint8_t port_number,
    const struct server_plugin *plugin
);

/* Return the plugin at port_number, or NULL if none. */
const struct server_plugin *find_server_plugin(
    struct server_plugin_registry *registry,
    uint8_t port_number
);

/* Count how many slots hold a non-NULL plugin pointer. */
size_t count_registered_server_plugins(
    const struct server_plugin_registry *registry
);

#endif
