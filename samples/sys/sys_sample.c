/*
 * sys_sample.c
 *
 * Command-line client for the squidsys plugin running inside
 * squid-server.  Connects over the local Unix socket transport,
 * sends a single plain-text command on wire channel 1, and
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

#include <squid/snet.h>
#include <squid/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Wire channel used to reach the squidsys plugin.
 * Must match the port number assigned to libsquidsys.so in
 * squid_server_local.conf (plugin 1 ... → wire channel 1).
 */
#define SYS_CHANNEL 1

#define SQUID_RING_SIZE 255U
#define PACKET_MAX (SQUID_RING_SIZE - 1U)

static const squid_timing_t timing = { 6U, 0U, 0U, 3U };

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
    int squid_fd;
    const char *command;
    size_t command_len;
    uint8_t rx_buf[PACKET_MAX];
    uint8_t tx_ring[SQUID_RING_SIZE];
    uint8_t rx_ring[SQUID_RING_SIZE];
    int received = 0;

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

    if (command_len > PACKET_MAX) {
        fprintf(stderr, "error: command is too long (maximum %u bytes)\n",
                (unsigned int)PACKET_MAX);
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
        tx_ring,
        (uint8_t)sizeof(tx_ring),
        rx_ring,
        (uint8_t)sizeof(rx_ring)
    );
    if (squid_fd < 0) {
        fprintf(stderr, "error: squid_open failed\n");
        local_transport_close(&transport);
        return 1;
    }

    squid_connect(squid_fd, SYS_CHANNEL);

    /* Wait for the protocol handshake to complete. */
    while (!snet_link_is_up()) {
        snet_burst();
        usleep(5000);
    }

    if (squid_send(
        squid_fd,
        (const uint8_t *)command,
        (uint16_t)command_len
    ) < 0) {
        fprintf(stderr, "error: send failed — server may have disconnected\n");
        squid_close(squid_fd);
        local_transport_close(&transport);
        return 1;
    }

    /* Poll until a response arrives or the link drops. */
    while ((received <= 0) && snet_link_is_up()) {
        snet_burst();
        received = squid_recv(squid_fd, rx_buf, (uint16_t)sizeof(rx_buf));
        if (received <= 0) {
            usleep(5000);
        }
    }

    if (received > 0) {
        fprintf(stdout, "%.*s\n", received, (char *)rx_buf);
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
