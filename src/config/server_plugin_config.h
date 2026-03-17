/*
 * server_plugin_config.h
 *
 * Data types and parser interface for the plugin configuration
 * file that maps wire-channel port numbers to shared-library
 * paths.  The parsed result is a server_plugin_config_file
 * struct consumed by the runtime at startup.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_PLUGIN_CONFIG_H
#define SERVER_PLUGIN_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "squid_server/plugin_api.h"

/* Default paths used when no --config option is supplied. */
#define server_default_config_path \
    "/opt/squid/etc/squid_server.conf"
#define server_default_system_plugin_path \
    "/opt/squid/lib/plugins/libsquidsys.so"

/* Maximum byte length of a plugin .so path (including NUL). */
#define server_plugin_path_max 512

/* At most one plugin per assignable port (1-15). */
#define server_plugin_mapping_max server_plugin_port_max

/* One "plugin <port> <path>" line from the config file. */
struct server_plugin_mapping {
    uint8_t port_number;
    char    plugin_path[server_plugin_path_max];
};

/* Complete parsed plugin configuration. */
struct server_plugin_config_file {
    /* Path to the mandatory system plugin (squidsys). */
    char system_plugin_path[server_plugin_path_max];
    /* Optional per-port plugin assignments. */
    struct server_plugin_mapping mappings[server_plugin_mapping_max];
    size_t mapping_count;
};

/* Zero-initialise and set defaults for a config struct. */
void init_server_plugin_config_file(
    struct server_plugin_config_file *config_file
);

/*
 * Parse the file at file_path into *config_file.
 * On failure *error_message points to a static string.
 * Returns 0 on success, -1 on error.
 */
int parse_server_plugin_config_file(
    struct server_plugin_config_file *config_file,
    const char *file_path,
    const char **error_message
);

#endif
