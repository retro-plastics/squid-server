/*
 * server_plugin_loader.h
 *
 * Structures and API for loading and unloading plugin shared
 * libraries at runtime.  Each loaded plugin is tracked in a
 * loaded_server_plugin entry so that dlclose() and stop() can
 * be called symmetrically on shutdown.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_PLUGIN_LOADER_H
#define SERVER_PLUGIN_LOADER_H

#include "config/server_plugin_config.h"
#include "plugin/server_plugin_registry.h"

/* Book-keeping for one successfully loaded plugin. */
struct loaded_server_plugin {
    uint8_t                   port_number;
    void                     *library_handle; /* from dlopen() */
    const struct server_plugin *plugin;       /* from factory */
    char plugin_path[server_plugin_path_max]; /* for diagnostics */
};

/* Holds all plugins opened during a server run. */
struct server_plugin_loader {
    struct loaded_server_plugin loaded_plugins[server_port_count];
    size_t loaded_count;
};

/* Zero-initialise the loader. */
void init_server_plugin_loader(struct server_plugin_loader *loader);

/*
 * dlopen() the library at plugin_path, resolve get_server_plugin(),
 * call start(), and register the plugin at port_number.
 * error_buffer receives a human-readable reason on failure.
 * Returns 0 on success, -1 on error.
 */
int load_server_plugin(
    struct server_plugin_loader *loader,
    struct server_plugin_registry *registry,
    uint8_t port_number,
    const char *plugin_path,
    char *error_buffer,
    size_t error_buffer_size
);

/*
 * Call stop() and dlclose() for every plugin in reverse load
 * order, then clear their registry slots.
 */
void unload_server_plugins(
    struct server_plugin_loader *loader,
    struct server_plugin_registry *registry
);

#endif
