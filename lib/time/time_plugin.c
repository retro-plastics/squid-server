/*
 * time_plugin.c
 *
 * Read-only PC time-and-date service for squid clients.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#include "time_core.h"
#include "squid_server/plugin_api.h"

static int handle_squid_time_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    int response_size = 0;

    (void)plugin;
    (void)port_number;

    if ((request == NULL) || (request->packet_data == NULL) ||
        (request->packet_size == 0U) || (response == NULL) ||
        (response->packet_data == NULL) || (response->packet_capacity == 0U)) {
        return -1;
    }

    response_size = handle_squid_time_request(
        request->packet_data,
        request->packet_size,
        response->packet_data,
        response->packet_capacity
    );
    if (response_size < 0) {
        return -1;
    }
    response->packet_size = (size_t)response_size;
    return 0;
}

static struct server_plugin squid_time_plugin = {
    .plugin_name = "time",
    .plugin_context = NULL,
    .start = NULL,
    .stop = NULL,
    .handle_packet = handle_squid_time_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &squid_time_plugin;
}
