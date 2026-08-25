/*
 * sys_sample.c
 *
 * Command-line client for the squidsys plugin running inside
 * squid-server.  Connects over the local Unix socket transport,
 * sends a single plain-text command on wire channel 2, and
 * prints the reply.
 *
 * Usage:
 *   sys_sample <command>
 *
 * Commands:
 *   id   print server hostname and OS name/version
 *
 * NOTES:
 *  _XOPEN_SOURCE 600 is required for usleep().
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#define _XOPEN_SOURCE 600

#include "transport/local/local_transport.h"

#include "squid_client/client.h"

#include <squid/snet.h>
#include <squid/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Wire channel used to reach the squidsys plugin.
 * Must match the port number assigned to libsquidsys.so in
 * squid_server_local.conf (plugin 2 ... → wire channel 2).
 */
#define SYS_CHANNEL 2

static const squid_timing_t timing = { 6U, 0U, 0U, 3U };

static int pump_squid(void *context)
{
    (void)context;
    snet_burst();
    usleep(5000);
    return 0;
}

static void print_usage(const char *program)
{
    fprintf(stderr, "usage: %s <command>\n"
                    "commands:\n"
                    "  id   print server hostname and OS\n",
            program);
}

int main(int argc, char **argv)
{
    struct server_local_transport transport;
    struct squid_client client;
    int squid_fd;
    const char *command;
    size_t command_len;
    uint8_t workspace[SQUID_CLIENT_WORKSPACE_SIZE];
    struct squid_client_bytes reply;

    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    command = argv[1];
    command_len = strlen(command);

    if (command_len == 0U) {
        print_usage(argv[0]);
        return 1;
    }

    if (command_len > SQUID_CLIENT_PACKET_MAX) {
        fprintf(stderr, "error: command is too long (maximum %u bytes)\n",
                (unsigned int)SQUID_CLIENT_PACKET_MAX);
        return 1;
    }

    if (local_transport_connect(
        &transport,
        local_transport_default_socket_path
    ) != 0) {
        fprintf(stderr, "error: cannot connect to %s\n"
                        "       is squid-server running?\n",
                local_transport_default_socket_path);
        return 1;
    }

    local_transport_activate(&transport);
    snet_init(&transport.base.platform, &timing);

    squid_fd = squid_open(
        SQUID_CLIENT_WORKSPACE_SIZE,
        SQUID_CLIENT_WORKSPACE_SIZE
    );
    if (squid_fd < 0) {
        fprintf(stderr, "error: squid_open failed\n");
        local_transport_close(&transport);
        return 1;
    }

    squid_connect(squid_fd, SYS_CHANNEL);
    squid_client_init(
        &client,
        squid_fd,
        workspace,
        SQUID_CLIENT_PACKET_MAX,
        pump_squid,
        NULL
    );

    /* Wait for the protocol handshake to complete. */
    while (!snet_link_is_up()) {
        snet_burst();
        usleep(5000);
    }

    if ((strcmp(command, "id") != 0) ||
        (squid_client_system_id(&client, &reply) != 0)) {
        fprintf(stderr, "error: send failed — server may have disconnected\n");
        squid_close(squid_fd);
        local_transport_close(&transport);
        return 1;
    }

    if (reply.size > 0U) {
        fprintf(stdout, "%.*s\n", (int)reply.size, (const char *)reply.data);
        fflush(stdout);
    } else {
        fprintf(stderr, "error: link lost or no response from server\n");
        squid_close(squid_fd);
        local_transport_close(&transport);
        return 1;
    }

    squid_close(squid_fd);
    local_transport_close(&transport);
    return 0;
}
