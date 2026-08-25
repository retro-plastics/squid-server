#include "client_internal.h"
#include "squid_client/system.h"

int squid_client_system_id(
    struct squid_client *client,
    struct squid_client_bytes *reply
)
{
    int response_size = 0;

    if ((client == 0) || (client->packet == 0) || (reply == 0) ||
        (client->packet_capacity < 2U)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    client->packet[1] = (uint8_t)'i';
    client->packet[2] = (uint8_t)'d';
    response_size = squid_client_exchange(client, 2U);
    if (response_size < 0) {
        return response_size;
    }
    reply->data = client->packet;
    reply->size = (uint8_t)response_size;
    return 0;
}
