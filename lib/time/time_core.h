#ifndef SQUID_TIME_CORE_H
#define SQUID_TIME_CORE_H

#include <stddef.h>
#include <stdint.h>

int handle_squid_time_request(
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
);

#endif
