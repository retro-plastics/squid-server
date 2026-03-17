#ifndef SERVER_PLUGIN_REGISTRY_H
#define SERVER_PLUGIN_REGISTRY_H

#include "squid_server/plugin_api.h"

#include <stddef.h>

struct server_plugin_slot {
    uint8_t port_number;
    const struct server_plugin *plugin;
};

struct server_plugin_registry {
    struct server_plugin_slot slots[server_port_count];
};

void init_server_plugin_registry(struct server_plugin_registry *registry);
int register_server_plugin(
    struct server_plugin_registry *registry,
    uint8_t port_number,
    const struct server_plugin *plugin
);
const struct server_plugin *find_server_plugin(
    struct server_plugin_registry *registry,
    uint8_t port_number
);
size_t count_registered_server_plugins(const struct server_plugin_registry *registry);

#endif
