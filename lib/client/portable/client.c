/* Portable synchronous squid-server transaction layer. */
#include "squid_client/base.h"

#include <squid/snet.h>
#include <squid/socket.h>

static int wait_for_progress(struct squid_client *client, uint8_t epoch)
{
    if ((client->idle != 0) && client->idle(client->idle_context)) {
        return SQUID_CLIENT_ERROR_CANCELLED;
    }
    if (!snet_link_is_up() || (snet_link_epoch() != epoch)) {
        return SQUID_CLIENT_ERROR_LINK;
    }
    return 0;
}

static int receive_bytes(
    struct squid_client *client,
    uint8_t *destination,
    uint16_t size,
    uint8_t epoch
)
{
    uint16_t received = 0U;

    while (received < size) {
        int result = squid_recv(
            client->socket_fd,
            destination + received,
            (uint16_t)(size - received)
        );
        int wait_result = 0;

        if (result < 0) {
            return SQUID_CLIENT_ERROR_IO;
        }
        received = (uint16_t)(received + (uint16_t)result);
        if (received == size) {
            return 0;
        }
        wait_result = wait_for_progress(client, epoch);
        if (wait_result != 0) {
            return wait_result;
        }
    }
    return 0;
}

void squid_client_init(
    struct squid_client *client,
    int socket_fd,
    uint8_t *workspace,
    uint16_t packet_capacity,
    squid_client_idle_fn idle,
    void *idle_context
)
{
    if (client == 0) {
        return;
    }
    client->socket_fd = socket_fd;
    client->packet = workspace;
    client->packet_capacity = packet_capacity;
    client->idle = idle;
    client->idle_context = idle_context;
}

int squid_client_exchange(struct squid_client *client, uint16_t request_size)
{
    uint8_t response_size = 0U;
    uint8_t epoch = 0U;
    int result = 0;

    if ((client == 0) || (client->packet == 0) ||
        (client->packet_capacity == 0U) ||
        (client->packet_capacity > SQUID_CLIENT_PACKET_MAX) ||
        (request_size == 0U) || (request_size > client->packet_capacity)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    if (!snet_link_is_up()) {
        return SQUID_CLIENT_ERROR_LINK;
    }

    epoch = snet_link_epoch();
    client->packet[0] = (uint8_t)request_size;
    result = squid_send(
        client->socket_fd,
        client->packet,
        (uint16_t)(request_size + 1U)
    );
    if (result < 0) {
        return SQUID_CLIENT_ERROR_IO;
    }

    result = receive_bytes(client, &response_size, 1U, epoch);
    if (result != 0) {
        return result;
    }
    if (response_size == 0U) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }

    if ((uint16_t)response_size > client->packet_capacity) {
        uint16_t remaining = response_size;
        while (remaining > 0U) {
            uint16_t part = remaining;
            if (part > client->packet_capacity) {
                part = client->packet_capacity;
            }
            result = receive_bytes(client, client->packet, part, epoch);
            if (result != 0) {
                return result;
            }
            remaining = (uint16_t)(remaining - part);
        }
        return SQUID_CLIENT_ERROR_OVERFLOW;
    }

    result = receive_bytes(client, client->packet, response_size, epoch);
    return result == 0 ? (int)response_size : result;
}
