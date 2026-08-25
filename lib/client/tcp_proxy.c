#include "client_internal.h"
#include "squid_client/tcp_proxy.h"

int squid_client_tcp_connect(
    struct squid_client *client,
    const char *host,
    uint16_t port,
    uint8_t *family
)
{
    uint8_t *request = 0;
    int host_size = squid_client_text_size(host, SQUID_TCP_HOST_MAX);
    uint16_t request_size = 0U;
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) || (family == 0) ||
        (host_size <= 0) || (port == 0U)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(4U + (uint16_t)host_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_TCP_OP_CONNECT;
    squid_client_write_u16(request + 1U, port);
    request[3] = (uint8_t)host_size;
    squid_client_copy(request + 4U, host, (uint16_t)host_size);
    result = squid_client_response(
        client, request_size, SQUID_TCP_OP_CONNECT, 3U, &response_size);
    if (result != 0) {
        return result;
    }
    if ((response_size != 3) ||
        ((client->packet[2] != SQUID_TCP_FAMILY_IPV4) &&
         (client->packet[2] != SQUID_TCP_FAMILY_IPV6))) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    *family = client->packet[2];
    return 0;
}

int squid_client_tcp_write(
    struct squid_client *client,
    const void *data,
    uint8_t size,
    uint8_t *written
)
{
    uint8_t *request = 0;
    uint16_t request_size = (uint16_t)(2U + size);
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) || (data == 0) ||
        (written == 0) || (size == 0U) ||
        (request_size > client->packet_capacity)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_TCP_OP_WRITE;
    request[1] = size;
    squid_client_copy(request + 2U, data, size);
    result = squid_client_response(
        client, request_size, SQUID_TCP_OP_WRITE, 3U, &response_size);
    if (result != 0) {
        return result;
    }
    if ((response_size != 3) || (client->packet[2] > size)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    *written = client->packet[2];
    return 0;
}

int squid_client_tcp_read(
    struct squid_client *client,
    uint16_t maximum_wait_ms,
    uint8_t maximum_bytes,
    struct squid_client_tcp_read *chunk
)
{
    uint8_t *request = 0;
    uint8_t data_size = 0U;
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) || (chunk == 0) ||
        (maximum_wait_ms > SQUID_TCP_READ_WAIT_MAX_MS) ||
        (client->packet_capacity < 4U)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_TCP_OP_READ;
    squid_client_write_u16(request + 1U, maximum_wait_ms);
    request[3] = maximum_bytes;
    result = squid_client_response(
        client, 4U, SQUID_TCP_OP_READ, 4U, &response_size);
    if (result != 0) {
        return result;
    }
    data_size = client->packet[3];
    if ((response_size != (int)(4U + data_size)) ||
        ((client->packet[2] & (uint8_t)~SQUID_TCP_READ_EOF) != 0U)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    chunk->eof = (uint8_t)((client->packet[2] & SQUID_TCP_READ_EOF) != 0U);
    chunk->data.data = client->packet + 4U;
    chunk->data.size = data_size;
    return 0;
}

int squid_client_tcp_close(struct squid_client *client)
{
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) ||
        (client->packet_capacity < 1U)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    client->packet[1] = SQUID_TCP_OP_CLOSE;
    result = squid_client_response(
        client, 1U, SQUID_TCP_OP_CLOSE, 2U, &response_size);
    if (result != 0) {
        return result;
    }
    return response_size == 2 ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
}

int squid_client_tcp_status(struct squid_client *client, uint8_t *connected)
{
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) || (connected == 0) ||
        (client->packet_capacity < 1U)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    client->packet[1] = SQUID_TCP_OP_STATUS;
    result = squid_client_response(
        client, 1U, SQUID_TCP_OP_STATUS, 3U, &response_size);
    if (result != 0) {
        return result;
    }
    if ((response_size != 3) || (client->packet[2] > 1U)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    *connected = client->packet[2];
    return 0;
}
