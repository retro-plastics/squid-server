#define _POSIX_C_SOURCE 200809L

#include "time_core.h"
#include "squid_server/time_protocol.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
}

static void write_i16(uint8_t *output, int16_t value)
{
    write_u16(output, (uint16_t)value);
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static int write_status(
    uint8_t opcode,
    uint8_t status,
    uint8_t *response,
    size_t response_capacity
)
{
    if ((response == NULL) || (response_capacity < 2U)) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_TIME_RESPONSE_BIT);
    response[1] = status;
    return 2;
}

static int parse_utc_offset(const struct tm *local_time, int16_t *offset)
{
    char text[8];
    size_t text_size = strftime(text, sizeof(text), "%z", local_time);
    int sign = 1;
    int hours = 0;
    int minutes = 0;
    int total = 0;

    if ((text_size != 5U) || ((text[0] != '+') && (text[0] != '-')) ||
        !isdigit((unsigned char)text[1]) ||
        !isdigit((unsigned char)text[2]) ||
        !isdigit((unsigned char)text[3]) ||
        !isdigit((unsigned char)text[4])) {
        return -1;
    }

    sign = text[0] == '-' ? -1 : 1;
    hours = ((text[1] - '0') * 10) + (text[2] - '0');
    minutes = ((text[3] - '0') * 10) + (text[4] - '0');
    if ((hours > 23) || (minutes > 59)) {
        return -1;
    }
    total = sign * ((hours * 60) + minutes);
    if ((total < INT16_MIN) || (total > INT16_MAX)) {
        return -1;
    }
    *offset = (int16_t)total;
    return 0;
}

static int handle_capabilities(
    uint8_t opcode,
    uint8_t *response,
    size_t response_capacity
)
{
    if (response_capacity < 5U) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_TIME_RESPONSE_BIT);
    response[1] = SQUID_TIME_STATUS_OK;
    response[2] = SQUID_TIME_PROTOCOL_VERSION;
    response[3] = SQUID_TIME_FEATURE_UTC |
        SQUID_TIME_FEATURE_LOCAL | SQUID_TIME_FEATURE_UNIX;
    response[4] = SQUID_TIME_REPLY_SIZE;
    return 5;
}

static int handle_get(
    uint8_t opcode,
    uint8_t mode,
    uint8_t *response,
    size_t response_capacity
)
{
    time_t now = time(NULL);
    struct tm broken_down;
    struct tm *result = NULL;
    int16_t utc_offset = 0;
    uint32_t unix_time = SQUID_TIME_UNIX_UNKNOWN;
    uint8_t weekday = 0U;

    if (response_capacity < SQUID_TIME_REPLY_SIZE) {
        return -1;
    }
    if (now == (time_t)-1) {
        return write_status(
            opcode,
            SQUID_TIME_STATUS_UNAVAILABLE,
            response,
            response_capacity
        );
    }

    if (mode == SQUID_TIME_MODE_UTC) {
        result = gmtime_r(&now, &broken_down);
    } else {
        result = localtime_r(&now, &broken_down);
        if ((result != NULL) &&
            (parse_utc_offset(&broken_down, &utc_offset) != 0)) {
            return write_status(
                opcode,
                SQUID_TIME_STATUS_UNAVAILABLE,
                response,
                response_capacity
            );
        }
    }
    if (result == NULL) {
        return write_status(
            opcode,
            SQUID_TIME_STATUS_UNAVAILABLE,
            response,
            response_capacity
        );
    }

    if ((now >= (time_t)0) && ((uint64_t)now <= UINT32_MAX)) {
        unix_time = (uint32_t)now;
    }
    weekday = (uint8_t)(broken_down.tm_wday & SQUID_TIME_WEEKDAY_MASK);
    if ((mode == SQUID_TIME_MODE_LOCAL) && (broken_down.tm_isdst > 0)) {
        weekday |= SQUID_TIME_DST_BIT;
    }

    response[0] = (uint8_t)(opcode | SQUID_TIME_RESPONSE_BIT);
    response[1] = SQUID_TIME_STATUS_OK;
    write_u16(response + 2U, (uint16_t)(broken_down.tm_year + 1900));
    response[4] = (uint8_t)(broken_down.tm_mon + 1);
    response[5] = (uint8_t)broken_down.tm_mday;
    response[6] = (uint8_t)broken_down.tm_hour;
    response[7] = (uint8_t)broken_down.tm_min;
    response[8] = (uint8_t)broken_down.tm_sec;
    response[9] = weekday;
    write_i16(response + 10U, utc_offset);
    write_u32(response + 12U, unix_time);
    return SQUID_TIME_REPLY_SIZE;
}

int handle_squid_time_request(
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint8_t opcode = 0U;

    if ((request == NULL) || (request_size == 0U) || (response == NULL) ||
        (response_capacity < 2U)) {
        return -1;
    }

    opcode = request[0];
    if (opcode == SQUID_TIME_OP_CAPABILITIES) {
        if (request_size != 1U) {
            return write_status(
                opcode,
                SQUID_TIME_STATUS_BAD_REQUEST,
                response,
                response_capacity
            );
        }
        return handle_capabilities(opcode, response, response_capacity);
    }
    if (opcode == SQUID_TIME_OP_GET) {
        if ((request_size != 2U) ||
            ((request[1] != SQUID_TIME_MODE_UTC) &&
             (request[1] != SQUID_TIME_MODE_LOCAL))) {
            return write_status(
                opcode,
                SQUID_TIME_STATUS_BAD_REQUEST,
                response,
                response_capacity
            );
        }
        return handle_get(opcode, request[1], response, response_capacity);
    }

    return write_status(
        opcode, SQUID_TIME_STATUS_BAD_REQUEST, response, response_capacity);
}
