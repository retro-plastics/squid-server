#ifndef SQUID_CLIENT_INTERNAL_H
#define SQUID_CLIENT_INTERNAL_H

#include "squid_client/base.h"

#include <stdint.h>

uint16_t squid_client_read_u16(const uint8_t *bytes);
uint32_t squid_client_read_u32(const uint8_t *bytes);
void squid_client_write_u16(uint8_t *bytes, uint16_t value);
void squid_client_write_u32(uint8_t *bytes, uint32_t value);
int squid_client_text_size(const char *text, uint8_t maximum);
void squid_client_copy(
    uint8_t *destination,
    const void *source,
    uint16_t size
);
int squid_client_response(
    struct squid_client *client,
    uint16_t request_size,
    uint8_t opcode,
    uint8_t minimum_size,
    int *response_size
);

#endif
