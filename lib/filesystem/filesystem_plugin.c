/*
 * filesystem_plugin.c
 *
 * Rooted binary filesystem service for low-resource squid clients.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 */

#include "filesystem_core.h"
#include "squid_server/plugin_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define squid_fs_default_root "/opt/squid/share"

struct squid_fs_plugin_state {
    struct squid_fs_context context;
    int started;
};

static struct squid_fs_plugin_state plugin_state;

static int environment_is_true(const char *name)
{
    const char *value = getenv(name);

    return (value != NULL) &&
        ((strcmp(value, "1") == 0) || (strcmp(value, "true") == 0) ||
         (strcmp(value, "yes") == 0));
}

static int get_root_path(char *path, size_t path_capacity)
{
    const char *configured = getenv("SQUID_FS_ROOT");
    const char *stage_root = getenv("SQUID_SERVER_STAGE_ROOT");
    int written = 0;

    if ((configured != NULL) && (configured[0] != '\0')) {
        written = snprintf(path, path_capacity, "%s", configured);
    } else if ((stage_root != NULL) && (stage_root[0] != '\0')) {
        written = snprintf(path, path_capacity, "%s%s", stage_root,
            squid_fs_default_root);
    } else {
        written = snprintf(path, path_capacity, "%s", squid_fs_default_root);
    }

    return ((written >= 0) && ((size_t)written < path_capacity)) ? 0 : -1;
}

static int start_squid_fs_plugin(struct server_plugin *plugin)
{
    struct squid_fs_plugin_state *state = NULL;
    char root_path[1024];

    if ((plugin == NULL) || (plugin->plugin_context == NULL) ||
        (get_root_path(root_path, sizeof(root_path)) != 0)) {
        return -1;
    }

    state = plugin->plugin_context;
    if (state->started) {
        return 0;
    }

    memset(state, 0, sizeof(*state));
    state->context.root_fd = -1;
    if (init_squid_fs_context(
        &state->context,
        root_path,
        environment_is_true("SQUID_FS_READ_ONLY")
    ) != 0) {
        return -1;
    }

    state->started = 1;
    return 0;
}

static int stop_squid_fs_plugin(struct server_plugin *plugin)
{
    struct squid_fs_plugin_state *state = NULL;

    if ((plugin == NULL) || (plugin->plugin_context == NULL)) {
        return -1;
    }

    state = plugin->plugin_context;
    if (state->started) {
        free_squid_fs_context(&state->context);
        state->started = 0;
    }
    return 0;
}

static int handle_squid_fs_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    struct squid_fs_plugin_state *state = NULL;
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

    response_size = handle_squid_fs_request(
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

static struct server_plugin squid_fs_plugin = {
    .plugin_name = "filesystem",
    .plugin_context = &plugin_state,
    .start = start_squid_fs_plugin,
    .stop = stop_squid_fs_plugin,
    .handle_packet = handle_squid_fs_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &squid_fs_plugin;
}
