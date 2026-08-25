#include "client_internal.h"
#include "squid_client/echo.h"

int squid_client_echo(
    struct squid_client *client,
    const void *data,
    uint8_t size,
    struct squid_client_bytes *reply
)
{
    int response_size = 0;

    if ((client == 0) || (client->packet == 0) || (data == 0) ||
        (size == 0U) || (reply == 0) ||
        ((uint16_t)size > client->packet_capacity)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    squid_client_copy(client->packet + 1U, data, size);
    response_size = squid_client_exchange(client, size);
    if (response_size < 0) {
        return response_size;
    }
    reply->data = client->packet;
    reply->size = (uint8_t)response_size;
    return 0;
}
