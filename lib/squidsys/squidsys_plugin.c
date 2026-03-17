#include "squid_server/plugin_api.h"

#include <string.h>

static int start_squidsys_plugin(struct server_plugin *plugin)
{
    (void)plugin;
    return 0;
}

static int stop_squidsys_plugin(struct server_plugin *plugin)
{
    (void)plugin;
    return 0;
}

static int handle_squidsys_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    static const char reply_text[] = "squidsys";
    size_t reply_size = sizeof(reply_text) - 1U;

    (void)plugin;
    (void)request;

    if ((port_number != server_system_port) || (response == NULL)) {
        return -1;
    }

    if ((response->packet_data == NULL) || (response->packet_capacity < reply_size)) {
        return -1;
    }

    memcpy(response->packet_data, reply_text, reply_size);
    response->packet_size = reply_size;
    return 0;
}

static struct server_plugin squidsys_plugin = {
    .plugin_name = "squidsys",
    .plugin_context = NULL,
    .start = start_squidsys_plugin,
    .stop = stop_squidsys_plugin,
    .handle_packet = handle_squidsys_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &squidsys_plugin;
}
