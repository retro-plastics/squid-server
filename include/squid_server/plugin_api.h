/*
 * plugin_api.h
 *
 * Public ABI that every squid-server plugin shared library must
 * implement.  A plugin exposes exactly one symbol:
 *
 *   const struct server_plugin *get_server_plugin(void);
 *
 * The returned struct carries function pointers for start(),
 * stop(), and handle_packet().  The server calls start() after
 * dlopen() and stop() before dlclose().  handle_packet() is
 * called for every packet received on the plugin's wire channel.
 *
 * NOTES:
 *  This header is installed to the staging tree so that plugin
 *  projects can build against it without including server
 *  internals.  Keep it self-contained (only stddef.h / stdint.h).
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SQUID_SERVER_PLUGIN_API_H
#define SQUID_SERVER_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

/* Port number constants. */
#define server_system_port     0   /* reserved for squidsys */
#define server_plugin_port_min 1   /* first assignable port */
#define server_plugin_port_max 15  /* last assignable port  */
#define server_port_min        server_system_port
#define server_port_max        server_plugin_port_max
#define server_port_count      ((server_port_max - server_port_min) + 1)
#define server_plugin_api_version 1

/* Immutable view of an incoming packet payload. */
struct server_packet_view {
    const void *packet_data;
    size_t      packet_size;
};

/*
 * Writable buffer for a plugin's response.  The plugin sets
 * packet_size to indicate how many bytes it produced.
 */
struct server_packet_buffer {
    void  *packet_data;
    size_t packet_capacity;
    size_t packet_size;
};

struct server_plugin;

/* Called once after dlopen() to let the plugin initialise. */
typedef int (*server_plugin_start_fn)(struct server_plugin *plugin);

/* Called once before dlclose() to let the plugin clean up. */
typedef int (*server_plugin_stop_fn)(struct server_plugin *plugin);

/*
 * Called for every inbound packet.  Fill *response and return 0
 * to send a reply; return -1 to suppress the reply.
 */
typedef int (*server_plugin_handle_packet_fn)(
    struct server_plugin *plugin,
    uint8_t port_number,
    const struct server_packet_view *request,
    struct server_packet_buffer *response
);

/* Descriptor returned by get_server_plugin(). */
struct server_plugin {
    const char *plugin_name;    /* human-readable identifier  */
    void       *plugin_context; /* plugin-private state       */
    server_plugin_start_fn         start;
    server_plugin_stop_fn          stop;
    server_plugin_handle_packet_fn handle_packet;
};

/* Return non-zero if port_number is in the range 0-15. */
int is_valid_server_port(uint8_t port_number);

/* Return non-zero if port_number is in the range 1-15. */
int is_valid_assignable_server_port(uint8_t port_number);

/* Entry point that every plugin .so must export. */
const struct server_plugin *get_server_plugin(void);

#endif
