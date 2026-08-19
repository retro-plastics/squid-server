#include "time_core.h"
#include "squid_server/time_protocol.h"

#include <stdint.h>
#include <stdio.h>

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8U));
}

static int test_capabilities(void)
{
    const uint8_t request[] = { SQUID_TIME_OP_CAPABILITIES };
    uint8_t response[32];
    int size = handle_squid_time_request(
        request, sizeof(request), response, sizeof(response));

    return (size == 5) && (response[1] == SQUID_TIME_STATUS_OK) &&
        (response[2] == SQUID_TIME_PROTOCOL_VERSION) &&
        ((response[3] & SQUID_TIME_FEATURE_LOCAL) != 0U) &&
        (response[4] == SQUID_TIME_REPLY_SIZE) ? 0 : 1;
}

static int test_mode(uint8_t mode)
{
    const uint8_t request[] = { SQUID_TIME_OP_GET, mode };
    uint8_t response[32];
    uint16_t year = 0U;
    int size = handle_squid_time_request(
        request, sizeof(request), response, sizeof(response));

    if ((size != SQUID_TIME_REPLY_SIZE) ||
        (response[1] != SQUID_TIME_STATUS_OK)) {
        return 1;
    }
    year = read_u16(response + 2U);
    return (year >= 2020U) && (year <= 2200U) &&
        (response[4] >= 1U) && (response[4] <= 12U) &&
        (response[5] >= 1U) && (response[5] <= 31U) &&
        (response[6] <= 23U) && (response[7] <= 59U) &&
        (response[8] <= 60U) &&
        ((response[9] & SQUID_TIME_WEEKDAY_MASK) <= 6U) ? 0 : 1;
}

static int test_bad_request(void)
{
    const uint8_t request[] = { SQUID_TIME_OP_GET, 7U };
    uint8_t response[8];
    int size = handle_squid_time_request(
        request, sizeof(request), response, sizeof(response));

    return (size == 2) &&
        (response[1] == SQUID_TIME_STATUS_BAD_REQUEST) ? 0 : 1;
}

int main(void)
{
    int failed = 0;

#define RUN_TEST(test_call) \
    do { \
        if ((test_call) != 0) { \
            fprintf(stderr, "%s failed\n", #test_call); \
            failed = 1; \
        } \
    } while (0)

    RUN_TEST(test_capabilities());
    RUN_TEST(test_mode(SQUID_TIME_MODE_UTC));
    RUN_TEST(test_mode(SQUID_TIME_MODE_LOCAL));
    RUN_TEST(test_bad_request());

#undef RUN_TEST

    if (failed) {
        return 1;
    }
    puts("all time tests passed");
    return 0;
}
