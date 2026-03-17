#ifndef SQUID_SERVER_PLUGIN_API_H
#define SQUID_SERVER_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#define server_system_port 0
#define server_plugin_port_min 1
#define server_plugin_port_max 15
#define server_port_min server_system_port
#define server_port_max server_plugin_port_max
#define server_port_count ((server_port_max - server_port_min) + 1)
#define server_plugin_api_version 1

struct server_packet_view {
    const void *packet_data;
    size_t packet_size;
};

struct server_packet_buffer {
    void *packet_data;
    size_t packet_capacity;
    size_t packet_size;
};

struct server_plugin;

typedef int (*server_plugin_start_fn)(struct server_plugin *plugin);
typedef int (*server_plugin_stop_fn)(struct server_plugin *plugin);
typedef int (*server_plugin_handle_packet_fn)(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
);

struct server_plugin {
    const char *plugin_name;
    void *plugin_context;
    server_plugin_start_fn start;
    server_plugin_stop_fn stop;
    server_plugin_handle_packet_fn handle_packet;
};

int is_valid_server_port(uint8_t port_number);
int is_valid_assignable_server_port(uint8_t port_number);
const struct server_plugin *get_server_plugin(void);

#endif
