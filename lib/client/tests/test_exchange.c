#include "squid_client/client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t incoming[32];
static uint16_t incoming_size;
static uint16_t incoming_position;
static uint8_t sent[32];
static uint16_t sent_size;
static uint8_t epoch;
static bool link_up = true;
static int idle_calls;

bool snet_link_is_up(void) { return link_up; }
uint8_t snet_link_epoch(void) { return epoch; }

int squid_send(int fd, const uint8_t *data, uint16_t size)
{
    (void)fd;
    memcpy(sent, data, size);
    sent_size = size;
    return size;
}

int squid_recv(int fd, uint8_t *data, uint16_t maximum)
{
    (void)fd;
    (void)maximum;
    if (incoming_position == incoming_size) {
        return 0;
    }
    *data = incoming[incoming_position++];
    return 1;
}

static int idle(void *context)
{
    (void)context;
    ++idle_calls;
    return 0;
}

int main(void)
{
    uint8_t workspace[16];
    struct squid_client client;
    int size = 0;

    incoming[0] = 5U;
    memcpy(incoming + 1U, "reply", 5U);
    incoming_size = 6U;
    squid_client_init(&client, 3, workspace, 15U, idle, 0);
    memcpy(workspace + 1U, "request", 7U);

    size = squid_client_exchange(&client, 7U);
    if ((size != 5) || (sent_size != 8U) || (sent[0] != 7U) ||
        (memcmp(sent + 1U, "request", 7U) != 0) ||
        (memcmp(workspace, "reply", 5U) != 0) || (idle_calls == 0)) {
        fputs("squid client exchange failed\n", stderr);
        return 1;
    }

    incoming[0] = 5U;
    memcpy(incoming + 1U, "large", 5U);
    incoming_size = 6U;
    incoming_position = 0U;
    squid_client_init(&client, 3, workspace, 3U, idle, 0);
    workspace[1] = 0xaaU;
    size = squid_client_exchange(&client, 1U);
    if ((size != SQUID_CLIENT_ERROR_OVERFLOW) ||
        (incoming_position != incoming_size)) {
        fputs("squid client overflow drain failed\n", stderr);
        return 1;
    }

    link_up = false;
    if (squid_client_exchange(&client, 1U) != SQUID_CLIENT_ERROR_LINK) {
        fputs("squid client link check failed\n", stderr);
        return 1;
    }
    puts("squid client exchange: OK");
    return 0;
}
