/* Time plugin client. */
#ifndef SQUID_CLIENT_TIME_H
#define SQUID_CLIENT_TIME_H

#include "squid_client/base.h"
#include "squid_server/time_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct squid_client_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
    uint8_t daylight_saving;
    int16_t utc_offset_minutes;
    uint32_t unix_seconds;
} squid_client_time_t;

int squid_client_time_get(
    squid_client_t *client,
    uint8_t mode,
    squid_client_time_t *value
);

#ifdef __cplusplus
}
#endif

#endif
