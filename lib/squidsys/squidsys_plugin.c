/*
 * squidsys_plugin.c
 *
 * System information plugin for squid-server.  Handles simple
 * plain-text commands sent over the wire and returns structured
 * text responses.
 *
 * Supported commands:
 *   id   — reply "<hostname>\n<sysname> <release>"
 *
 * Unknown commands return -1; the server discards the response.
 *
 * NOTES:
 *  _POSIX_C_SOURCE 200112L is required for gethostname().
 *  The plugin is intentionally port-agnostic: it can be mounted
 *  at port 0 (system/internal) and at a wire-accessible port
 *  simultaneously, as the local test configuration does.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#define _POSIX_C_SOURCE 200112L

#include "squid_server/plugin_api.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

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

/*
 * Handle a packet addressed to the squidsys plugin.
 *
 * Supported commands (plain text, no NUL terminator in the packet):
 *
 *   id   — reply with the host's computer name and OS description,
 *           separated by a newline:
 *               <hostname>\n<sysname> <release>
 *
 * Unknown commands return -1 (error; the server discards the response).
 */
static int handle_squidsys_packet(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
)
{
    struct utsname info;
    char hostname[64];
    int written;

    (void)plugin;
    (void)port_number;

    if ((request == NULL) || (response == NULL)) {
        return -1;
    }

    if ((response->packet_data == NULL) || (response->packet_capacity == 0U)) {
        return -1;
    }

    /* "id" command: return hostname + OS description. */
    if ((request->packet_size == 2U) &&
        (memcmp(request->packet_data, "id", 2U) == 0)) {

        if (uname(&info) != 0) {
            return -1;
        }

        if (gethostname(hostname, sizeof(hostname)) != 0) {
            strncpy(hostname, "unknown", sizeof(hostname) - 1U);
            hostname[sizeof(hostname) - 1U] = '\0';
        }

        written = snprintf(
            (char *)response->packet_data,
            response->packet_capacity,
            "%s\n%s %s",
            hostname,
            info.sysname,
            info.release
        );

        if (written < 0) {
            return -1;
        }

        response->packet_size = ((size_t)written < response->packet_capacity)
            ? (size_t)written
            : response->packet_capacity;

        return 0;
    }

    return -1;  /* unknown command */
}

static struct server_plugin squidsys_plugin = {
    .plugin_name    = "squidsys",
    .plugin_context = NULL,
    .start          = start_squidsys_plugin,
    .stop           = stop_squidsys_plugin,
    .handle_packet  = handle_squidsys_packet
};

const struct server_plugin *get_server_plugin(void)
{
    return &squidsys_plugin;
}
