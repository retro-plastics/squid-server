/*
 * echo_sample.c
 *
 * Interactive echo test client for squid-server.  Connects to
 * the server over the local Unix socket transport, performs the
 * libsquid handshake, then enters a read-eval-print loop:
 * each line typed at stdin is sent to the echo plugin on wire
 * channel 2 and the server's verbatim reply is printed.
 * Press Ctrl+D to quit.
 *
 * Usage:
 *   Start the server first (see README), then run:
 *     ./bin/opt/squid/bin/echo_sample
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
 * Wire channel used to reach the echo plugin.
 * Must match the port number assigned to libecho.so in squid_server_local.conf
 * (plugin 2 /opt/squid/lib/plugins/libecho.so → wire channel 2).
 */
#define ECHO_CHANNEL 2

#define SQUID_RING_SIZE 255U
#define PACKET_MAX (SQUID_RING_SIZE - 1U)

static const squid_timing_t timing = { 6U, 0U, 0U, 3U };

int main(void)
{
    struct server_local_transport transport;
    int squid_fd;
    char line[PACKET_MAX + 1U];
    uint8_t rx_buf[PACKET_MAX];
    uint8_t tx_ring[SQUID_RING_SIZE];
    uint8_t rx_ring[SQUID_RING_SIZE];

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

    squid_connect(squid_fd, ECHO_CHANNEL);

    /* Wait for the protocol handshake to complete. */
    while (!snet_link_is_up()) {
        snet_burst();
        usleep(5000);
    }

    fprintf(stdout, "connected to squid-server (echo, channel %d)\n"
                    "type a message and press enter — ctrl+d to quit\n\n",
            ECHO_CHANNEL);

    while (fgets(line, (int)sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);
        int received = 0;

        /* Strip the trailing newline written by fgets. */
        if ((len > 0U) && (line[len - 1U] == '\n')) {
            line[--len] = '\0';
        }

        if (len == 0U) {
            continue;
        }

        if (squid_send(squid_fd, (const uint8_t *)line, (uint16_t)len) < 0) {
            fprintf(stderr, "error: send failed — server may have disconnected\n");
            break;
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
            fprintf(stdout, "< %.*s\n", received, (char *)rx_buf);
            fflush(stdout);
        } else {
            fprintf(stderr, "error: link lost while waiting for response\n");
            break;
        }
    }

    squid_close(squid_fd);
    local_transport_close(&transport);
    return 0;
}
