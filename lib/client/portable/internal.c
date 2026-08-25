#include "client_internal.h"

uint16_t squid_client_read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

uint32_t squid_client_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) |
        ((uint32_t)bytes[3] << 24U);
}

void squid_client_write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

void squid_client_write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

int squid_client_text_size(const char *text, uint8_t maximum)
{
    uint16_t size = 0U;
    if (text == 0) {
        return 0;
    }
    while (text[size] != '\0') {
        ++size;
        if (size > maximum) {
            return SQUID_CLIENT_ERROR_ARGUMENT;
        }
    }
    return (int)size;
}

void squid_client_copy(
    uint8_t *destination,
    const void *source,
    uint16_t size
)
{
    const uint8_t *bytes = (const uint8_t *)source;
    uint16_t index = 0U;
    for (index = 0U; index < size; ++index) {
        destination[index] = bytes[index];
    }
}

int squid_client_response(
    struct squid_client *client,
    uint16_t request_size,
    uint8_t opcode,
    uint8_t minimum_size,
    int *response_size
)
{
    int size = squid_client_exchange(client, request_size);
    if (size < 0) {
        return size;
    }
    if ((size < 2) ||
        (client->packet[0] != (uint8_t)(opcode | 0x80U))) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    if (client->packet[1] != 0U) {
        return (int)client->packet[1];
    }
    if (size < minimum_size) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    if (response_size != 0) {
        *response_size = size;
    }
    return 0;
}
