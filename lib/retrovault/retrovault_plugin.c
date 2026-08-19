/*
 * retrovault_plugin.c
 *
 * Binary squid gateway for the Retro Vault package catalog. HTTP and JSON
 * stay on the Linux server; an 8-bit peer sees only compact binary records.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#include "retrovault_core.h"
#include "retrovault_curl.h"
#include "squid_server/plugin_api.h"

#include <string.h>

struct retro_vault_plugin_state {
    struct retro_vault_context context;
    struct retro_vault_curl_client curl;
    int started;
};

static struct retro_vault_plugin_state plugin_state;

static int start_retro_vault_plugin(struct server_plugin *plugin)
{
    struct retro_vault_http_client http;
    struct retro_vault_plugin_state *state = NULL;

    if ((plugin == NULL) || (plugin->plugin_context == NULL)) {
        return -1;
    }

    state = plugin->plugin_context;
    if (state->started) {
        return 0;
    }

    memset(state, 0, sizeof(*state));
    if (init_retro_vault_curl_client(&state->curl) != 0) {
        return -1;
    }

    http.get = get_retro_vault_curl;
    http.user_data = &state->curl;
    init_retro_vault_context(&state->context, &http);
    state->started = 1;
    return 0;
}

static int stop_retro_vault_plugin(struct server_plugin *plugin)
{
    struct retro_vault_plugin_state *state = NULL;

    if ((plugin == NULL) || (plugin->plugin_context == NULL)) {
        return -1;
    }

    state = plugin->plugin_context;
    if (state->started) {
        free_retro_vault_context(&state->context);
        free_retro_vault_curl_client(&state->curl);
        state->started = 0;
    }
    return 0;
}

static int handle_retro_vault_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    struct retro_vault_plugin_state *state = NULL;
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

    response_size = handle_retro_vault_request(
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

static struct server_plugin retro_vault_plugin = {
    .plugin_name = "retrovault",
    .plugin_context = &plugin_state,
    .start = start_retro_vault_plugin,
    .stop = stop_retro_vault_plugin,
    .handle_packet = handle_retro_vault_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &retro_vault_plugin;
}
