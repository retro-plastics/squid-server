/*
 * server_plugin_loader.c
 *
 * Opens plugin shared libraries with dlopen(RTLD_NOW|RTLD_LOCAL),
 * resolves the get_server_plugin() factory symbol, calls the
 * plugin's start() callback, and registers the result in the
 * plugin registry.  Unloading is done in reverse load order via
 * unload_server_plugins().
 *
 * NOTES:
 *  Absolute plugin paths from the config file are prefixed with
 *  the SQUID_SERVER_STAGE_ROOT environment variable when it is
 *  set, allowing local test builds to redirect the default
 *  /opt/squid/... tree to a staging directory.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#include "plugin/server_plugin_loader.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef const struct server_plugin *(*get_server_plugin_fn)(void);

static int copy_text(char *destination, size_t destination_size, const char *source)
{
    size_t length = 0;

    if ((destination == NULL) || (source == NULL) || (destination_size == 0U)) {
        return -1;
    }

    length = strlen(source);
    if (length >= destination_size) {
        return -1;
    }

    memcpy(destination, source, length + 1U);
    return 0;
}

static void set_error_text(
    char *error_buffer,
    size_t error_buffer_size,
    const char *message
)
{
    if ((error_buffer == NULL) || (error_buffer_size == 0U)) {
        return;
    }

    if (copy_text(error_buffer, error_buffer_size, message) != 0) {
        error_buffer[0] = '\0';
    }
}

static int resolve_plugin_path(
    const char *plugin_path,
    char *resolved_path,
    size_t resolved_path_size
)
{
    const char *stage_root = NULL;
    int written_length = 0;

    if ((plugin_path == NULL) || (resolved_path == NULL) || (resolved_path_size == 0U)) {
        return -1;
    }

    stage_root = getenv("SQUID_SERVER_STAGE_ROOT");

    if ((stage_root != NULL) && (plugin_path[0] == '/')) {
        written_length = snprintf(
            resolved_path,
            resolved_path_size,
            "%s%s",
            stage_root,
            plugin_path
        );

        if ((written_length < 0) || ((size_t)written_length >= resolved_path_size)) {
            return -1;
        }

        return 0;
    }

    return copy_text(resolved_path, resolved_path_size, plugin_path);
}

static void clear_plugin_slot(
    struct server_plugin_registry *registry,
    uint8_t port_number
)
{
    size_t index = 0;

    if ((registry == NULL) || !is_valid_server_port(port_number)) {
        return;
    }

    index = (size_t)(port_number - server_port_min);
    registry->slots[index].plugin = NULL;
}

void init_server_plugin_loader(struct server_plugin_loader *loader)
{
    if (loader == NULL) {
        return;
    }

    memset(loader, 0, sizeof(*loader));
}

int load_server_plugin(
    struct server_plugin_loader *loader,
    struct server_plugin_registry *registry,
    uint8_t port_number,
    const char *plugin_path,
    char *error_buffer,
    size_t error_buffer_size
)
{
    void *library_handle = NULL;
    get_server_plugin_fn factory = NULL;
    const struct server_plugin *plugin = NULL;
    struct loaded_server_plugin *loaded_plugin = NULL;
    const char *dl_error = NULL;
    char resolved_plugin_path[server_plugin_path_max];

    if ((loader == NULL) || (registry == NULL) || (plugin_path == NULL)) {
        set_error_text(error_buffer, error_buffer_size, "invalid plugin loader input");
        return -1;
    }

    if (!is_valid_server_port(port_number)) {
        set_error_text(error_buffer, error_buffer_size, "invalid plugin port");
        return -1;
    }

    if (loader->loaded_count >= server_port_count) {
        set_error_text(error_buffer, error_buffer_size, "plugin loader is full");
        return -1;
    }

    if (resolve_plugin_path(
        plugin_path,
        resolved_plugin_path,
        sizeof(resolved_plugin_path)
    ) != 0) {
        set_error_text(error_buffer, error_buffer_size, "failed to resolve plugin path");
        return -1;
    }

    library_handle = dlopen(resolved_plugin_path, RTLD_NOW | RTLD_LOCAL);
    if (library_handle == NULL) {
        dl_error = dlerror();
        set_error_text(
            error_buffer,
            error_buffer_size,
            dl_error != NULL ? dl_error : "failed to open plugin library"
        );
        return -1;
    }

    dlerror();
    *(void **)(&factory) = dlsym(library_handle, "get_server_plugin");
    dl_error = dlerror();
    if ((factory == NULL) || (dl_error != NULL)) {
        set_error_text(
            error_buffer,
            error_buffer_size,
            dl_error != NULL ? dl_error : "missing get_server_plugin symbol"
        );
        dlclose(library_handle);
        return -1;
    }

    plugin = factory();
    if ((plugin == NULL) || (plugin->plugin_name == NULL)) {
        set_error_text(error_buffer, error_buffer_size, "plugin factory returned no plugin");
        dlclose(library_handle);
        return -1;
    }

    if (register_server_plugin(registry, port_number, plugin) != 0) {
        set_error_text(error_buffer, error_buffer_size, "plugin port is already assigned");
        dlclose(library_handle);
        return -1;
    }

    if ((plugin->start != NULL) && (plugin->start((struct server_plugin *)plugin) != 0)) {
        set_error_text(error_buffer, error_buffer_size, "plugin start callback failed");
        clear_plugin_slot(registry, port_number);
        dlclose(library_handle);
        return -1;
    }

    loaded_plugin = &loader->loaded_plugins[loader->loaded_count];
    loaded_plugin->port_number = port_number;
    loaded_plugin->library_handle = library_handle;
    loaded_plugin->plugin = plugin;

    if (copy_text(
        loaded_plugin->plugin_path,
        sizeof(loaded_plugin->plugin_path),
        plugin_path
    ) != 0) {
        loaded_plugin->plugin_path[0] = '\0';
    }

    ++loader->loaded_count;
    set_error_text(error_buffer, error_buffer_size, "");
    return 0;
}

void unload_server_plugins(
    struct server_plugin_loader *loader,
    struct server_plugin_registry *registry
)
{
    while ((loader != NULL) && (loader->loaded_count > 0U)) {
        struct loaded_server_plugin *loaded_plugin = NULL;

        --loader->loaded_count;
        loaded_plugin = &loader->loaded_plugins[loader->loaded_count];

        if ((loaded_plugin->plugin != NULL) && (loaded_plugin->plugin->stop != NULL)) {
            loaded_plugin->plugin->stop((struct server_plugin *)loaded_plugin->plugin);
        }

        clear_plugin_slot(registry, loaded_plugin->port_number);

        if (loaded_plugin->library_handle != NULL) {
            dlclose(loaded_plugin->library_handle);
        }

        memset(loaded_plugin, 0, sizeof(*loaded_plugin));
    }
}
