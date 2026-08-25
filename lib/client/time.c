#include "client_internal.h"
#include "squid_client/time.h"

int squid_client_time_get(
    struct squid_client *client,
    uint8_t mode,
    struct squid_client_time *value
)
{
    const uint8_t *response = 0;
    int size = 0;

    if ((client == 0) || (client->packet == 0) || (value == 0) ||
        ((mode != SQUID_TIME_MODE_UTC) && (mode != SQUID_TIME_MODE_LOCAL)) ||
        (client->packet_capacity < 2U)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    client->packet[1] = SQUID_TIME_OP_GET;
    client->packet[2] = mode;
    {
        int result = squid_client_response(
            client,
            2U,
            SQUID_TIME_OP_GET,
            SQUID_TIME_REPLY_SIZE,
            &size
        );
        if (result != 0) {
            return result;
        }
    }
    if (size != SQUID_TIME_REPLY_SIZE) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }

    response = client->packet;
    value->year = squid_client_read_u16(response + 2U);
    value->month = response[4];
    value->day = response[5];
    value->hour = response[6];
    value->minute = response[7];
    value->second = response[8];
    value->weekday = (uint8_t)(response[9] & SQUID_TIME_WEEKDAY_MASK);
    value->daylight_saving =
        (uint8_t)((response[9] & SQUID_TIME_DST_BIT) != 0U);
    value->utc_offset_minutes = (int16_t)squid_client_read_u16(response + 10U);
    value->unix_seconds = squid_client_read_u32(response + 12U);
    return 0;
}
