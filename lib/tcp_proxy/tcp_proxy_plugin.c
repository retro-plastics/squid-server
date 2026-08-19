/*
 * tcp_proxy_plugin.c
 *
 * Single-stream raw TCP proxy for low-resource squid clients.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#include "tcp_proxy_core.h"
#include "squid_server/plugin_api.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

struct squid_tcp_plugin_state {
    struct squid_tcp_context context;
    int started;
};

static struct squid_tcp_plugin_state plugin_state;

static unsigned int read_timeout_environment(const char *name)
{
    const char *text = getenv(name);
    char *after = NULL;
    unsigned long value = 0UL;

    if ((text == NULL) || (text[0] == '\0')) {
        return 0U;
    }
    errno = 0;
    value = strtoul(text, &after, 10);
    if ((errno != 0) || (*after != '\0') || (value > UINT_MAX)) {
        return 0U;
    }
    return (unsigned int)value;
}

static int start_squid_tcp_plugin(struct server_plugin *plugin)
{
    struct squid_tcp_plugin_state *state = NULL;

    if ((plugin == NULL) || (plugin->plugin_context == NULL)) {
        return -1;
    }
    state = plugin->plugin_context;
    if (state->started) {
        return 0;
    }

    init_squid_tcp_context(
        &state->context,
        getenv("SQUID_TCP_ALLOWED_HOSTS"),
        read_timeout_environment("SQUID_TCP_CONNECT_TIMEOUT_MS"),
        read_timeout_environment("SQUID_TCP_WRITE_TIMEOUT_MS")
    );
    state->started = 1;
    return 0;
}

static int stop_squid_tcp_plugin(struct server_plugin *plugin)
{
    struct squid_tcp_plugin_state *state = NULL;

    if ((plugin == NULL) || (plugin->plugin_context == NULL)) {
        return -1;
    }
    state = plugin->plugin_context;
    if (state->started) {
        free_squid_tcp_context(&state->context);
        state->started = 0;
    }
    return 0;
}

static int handle_squid_tcp_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    struct squid_tcp_plugin_state *state = NULL;
    int response_size = 0;

    (void)port_number;

    if ((plugin == NULL) || (plugin->plugin_context == NULL) ||
        (request == NULL) || (request->packet_data == NULL) ||
        (request->packet_size == 0U) || (response == NULL) ||
        (response->packet_data == NULL) || (response->packet_capacity == 0U)) {
        return -1;
    }
    state = plugin->plugin_context;
    if (!state->started) {
        return -1;
    }

    response_size = handle_squid_tcp_request(
        &state->context,
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

static struct server_plugin squid_tcp_plugin = {
    .plugin_name = "tcp_proxy",
    .plugin_context = &plugin_state,
    .start = start_squid_tcp_plugin,
    .stop = stop_squid_tcp_plugin,
    .handle_packet = handle_squid_tcp_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &squid_tcp_plugin;
}
