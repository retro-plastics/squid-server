#ifndef SERVER_PLUGIN_CONFIG_H
#define SERVER_PLUGIN_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "squid_server/plugin_api.h"

#define server_default_config_path "/opt/squid/etc/squid_server.conf"
#define server_default_system_plugin_path "/opt/squid/lib/plugins/libsquidsys.so"
#define server_plugin_path_max 512
#define server_plugin_mapping_max server_plugin_port_max

struct server_plugin_mapping {
    uint8_t port_number;
    char plugin_path[server_plugin_path_max];
};

struct server_plugin_config_file {
    char system_plugin_path[server_plugin_path_max];
    struct server_plugin_mapping mappings[server_plugin_mapping_max];
    size_t mapping_count;
};

void init_server_plugin_config_file(struct server_plugin_config_file *config_file);
int parse_server_plugin_config_file(
    struct server_plugin_config_file *config_file,
    const char *file_path,
    const char **error_message
);

#endif
