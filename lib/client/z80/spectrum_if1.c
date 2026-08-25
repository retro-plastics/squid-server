/* ZX Spectrum 48K Interface 1 platform glue for libsquid. */
#include "squid_client/spectrum_if1.h"

#include <squid/socket.h>

#include <stdlib.h>

extern int squid_client_spectrum_if1_send_char(uint8_t value);
extern int squid_client_spectrum_if1_recv_char(void);
extern uint8_t squid_client_spectrum_if1_get_tick(void);

static squid_platform_t spectrum_if1_platform;
static squid_timing_t spectrum_if1_timing;

void squid_client_spectrum_if1_platform(squid_platform_t *platform)
{
    if (platform == 0) {
        return;
    }
    platform->send_char = squid_client_spectrum_if1_send_char;
    platform->recv_char = squid_client_spectrum_if1_recv_char;
    platform->get_tick = squid_client_spectrum_if1_get_tick;
    platform->mem_alloc = malloc;
    platform->mem_free = free;
}

int squid_client_spectrum_if1_open(
    struct squid_client *client,
    uint8_t channel,
    uint8_t *workspace,
    uint16_t packet_capacity
)
{
    int socket_fd = -1;
    uint16_t queue_capacity = 0U;

    if ((client == 0) || (workspace == 0) ||
        (channel == 0U) || (channel > 15U) ||
        (packet_capacity == 0U) ||
        (packet_capacity > SQUID_CLIENT_PACKET_MAX)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }

    queue_capacity = (uint16_t)(packet_capacity + 1U);
    squid_client_spectrum_if1_platform(&spectrum_if1_platform);
    spectrum_if1_timing.timeout_ticks = 6U;
    spectrum_if1_timing.ack_delay_ticks = 0U;
    spectrum_if1_timing.ping_ticks = 0U;
    spectrum_if1_timing.max_retries = 3U;
    squid_client_spectrum_if1_reset();
    snet_init(&spectrum_if1_platform, &spectrum_if1_timing);

    socket_fd = squid_open(queue_capacity, queue_capacity);
    if (socket_fd < 0) {
        return SQUID_CLIENT_ERROR_IO;
    }
    if (squid_connect(socket_fd, channel) != 0) {
        squid_close(socket_fd);
        return SQUID_CLIENT_ERROR_IO;
    }

    squid_client_init(
        client,
        socket_fd,
        workspace,
        packet_capacity,
        0,
        0
    );
    return 0;
}
