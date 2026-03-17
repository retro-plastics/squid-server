#include "squid_server/plugin_api.h"

#include <string.h>

static int start_echo_plugin(struct server_plugin *plugin)
{
    (void)plugin;
    return 0;
}

static int stop_echo_plugin(struct server_plugin *plugin)
{
    (void)plugin;
    return 0;
}

static int handle_echo_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    size_t bytes_to_copy = 0;

    (void)plugin;
    (void)port_number;

    if ((request == NULL) || (response == NULL) || (response->packet_data == NULL)) {
        return -1;
    }

    if (request->packet_size > response->packet_capacity) {
        return -1;
    }

    bytes_to_copy = request->packet_size;

    if ((bytes_to_copy > 0U) && (request->packet_data != NULL)) {
        memcpy(response->packet_data, request->packet_data, bytes_to_copy);
    }

    response->packet_size = bytes_to_copy;
    return 0;
}

static struct server_plugin echo_plugin = {
    .plugin_name = "echo",
    .plugin_context = NULL,
    .start = start_echo_plugin,
    .stop = stop_echo_plugin,
    .handle_packet = handle_echo_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &echo_plugin;
}
