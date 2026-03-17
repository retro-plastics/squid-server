#ifndef SERVER_PLUGIN_LOADER_H
#define SERVER_PLUGIN_LOADER_H

#include "config/server_plugin_config.h"
#include "plugin/server_plugin_registry.h"

struct loaded_server_plugin {
    uint8_t port_number;
    void *library_handle;
    const struct server_plugin *plugin;
    char plugin_path[server_plugin_path_max];
};

struct server_plugin_loader {
    struct loaded_server_plugin loaded_plugins[server_port_count];
    size_t loaded_count;
};

void init_server_plugin_loader(struct server_plugin_loader *loader);
int load_server_plugin(
    struct server_plugin_loader *loader,
    struct server_plugin_registry *registry,
    uint8_t port_number,
    const char *plugin_path,
    char *error_buffer,
    size_t error_buffer_size
);
void unload_server_plugins(
    struct server_plugin_loader *loader,
    struct server_plugin_registry *registry
);

#endif
