#include "squid_client/client.h"
#include "squid_server/time_protocol.h"
#include "../../client_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t sent[256];
static uint16_t sent_size;
static uint8_t incoming[256];
static uint16_t incoming_size;
static uint16_t incoming_position;
static uint8_t receive_pause;
static uint8_t link_up = 1U;
static uint8_t link_epoch = 7U;
static uint8_t fail_send;

bool snet_link_is_up(void)
{
    return link_up != 0U;
}

uint8_t snet_link_epoch(void)
{
    return link_epoch;
}

int squid_send(int fd, const uint8_t *data, uint16_t size)
{
    (void)fd;
    if ((fail_send != 0U) || (size > sizeof(sent))) {
        return -1;
    }
    memcpy(sent, data, size);
    sent_size = size;
    return (int)size;
}

int squid_recv(int fd, uint8_t *destination, uint16_t maximum)
{
    (void)fd;
    if (maximum == 0U) {
        return -1;
    }
    if (receive_pause != 0U) {
        receive_pause = 0U;
        return 0;
    }
    if (incoming_position >= incoming_size) {
        return 0;
    }
    *destination = incoming[incoming_position++];
    receive_pause = 1U;
    return 1;
}

static void set_response(const uint8_t *body, uint8_t size)
{
    incoming[0] = size;
    memcpy(incoming + 1U, body, size);
    incoming_size = (uint16_t)(size + 1U);
    incoming_position = 0U;
    receive_pause = 1U;
    sent_size = 0U;
}

static int idle(void *context)
{
    uint16_t *count = (uint16_t *)context;
    ++*count;
    return 0;
}

static int cancel(void *context)
{
    (void)context;
    return 1;
}

static int check(int condition, const char *message)
{
    if (condition) {
        return 1;
    }
    printf("FAIL: %s\n", message);
    return 0;
}

/* Keep every public plugin call in the native Z80 link and exercise its ABI.
 * With the link down, request calls return immediately without needing a mock
 * response; iterator calls validate an empty page. */
static int exercise_complete_public_api(struct squid_client *client)
{
    struct squid_client_bytes bytes;
    struct squid_client_time time_value;
    struct squid_client_fs_stat fs_stat;
    struct squid_client_fs_list fs_list = { 0U, 0U, 0, 0 };
    struct squid_client_fs_entry fs_entry;
    struct squid_client_fs_read fs_read;
    struct squid_client_retro_list retro_list = { 0U, 0U, 0, 0 };
    struct squid_client_retro_entry retro_entry;
    struct squid_client_retro_info retro_info = { 0U, 0U, 0, 0 };
    struct squid_client_retro_value retro_value;
    struct squid_client_retro_download retro_download;
    struct squid_client_tcp_read tcp_read;
    uint8_t byte = 0U;
    volatile int result = 0;

    result += squid_client_echo(client, "x", 1U, &bytes);
    result += squid_client_system_id(client, &bytes);
    result += squid_client_time_get(client, SQUID_TIME_MODE_UTC, &time_value);
    result += squid_client_fs_stat(client, "x", &fs_stat);
    result += squid_client_fs_list(client, "", 0U, &fs_list);
    result += squid_client_fs_list_next(&fs_list, &fs_entry);
    result += squid_client_fs_read(client, "x", 0UL, 1U, &fs_read);
    result += squid_client_fs_write(client, "x", 0UL, 0U, "x", 1U, &byte);
    result += squid_client_fs_mkdir(client, "x");
    result += squid_client_fs_delete(client, "x");
    result += squid_client_fs_rename(client, "x", "y");
    result += squid_client_retro_list(client, "", "", 0U, &retro_list);
    result += squid_client_retro_search(
        client, "", "", "x", 0U, &retro_list);
    result += squid_client_retro_list_next(&retro_list, &retro_entry);
    result += squid_client_retro_info(client, "x", 0U, &retro_info);
    result += squid_client_retro_info_next(&retro_info, &retro_value);
    result += squid_client_retro_download(
        client, "x", "y", 0UL, 1U, &retro_download);
    result += squid_client_tcp_connect(client, "x", 80U, &byte);
    result += squid_client_tcp_write(client, "x", 1U, &byte);
    result += squid_client_tcp_read(client, 0U, 1U, &tcp_read);
    result += squid_client_tcp_close(client);
    result += squid_client_tcp_status(client, &byte);
    return result;
}

static int test_internal_assembly(struct squid_client *client)
{
    uint8_t bytes[8] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
    static const uint8_t source[] = { 9U, 8U, 7U };
    static const uint8_t ok_reply[] = { 0x81U, 0U, 0x55U };
    static const uint8_t status_reply[] = { 0x81U, 7U };
    static const uint8_t bad_reply[] = { 0x82U, 0U };
    int response_size = -1;

    squid_client_write_u16(bytes, 0x1234U);
    if (!check(bytes[0] == 0x34U && bytes[1] == 0x12U &&
               squid_client_read_u16(bytes) == 0x1234U,
               "Z80 u16 helpers preserve little endian")) {
        return 0;
    }
    squid_client_write_u32(bytes, 0x89abcdefUL);
    if (!check(bytes[0] == 0xefU && bytes[1] == 0xcdU &&
               bytes[2] == 0xabU && bytes[3] == 0x89U &&
               squid_client_read_u32(bytes) == 0x89abcdefUL,
               "Z80 u32 helpers preserve little endian")) {
        return 0;
    }
    squid_client_copy(bytes + 4U, source, sizeof(source));
    if (!check(bytes[4] == 9U && bytes[5] == 8U && bytes[6] == 7U,
               "Z80 copy helper copies the exact byte count") ||
        !check(squid_client_text_size(0, 3U) == 0 &&
               squid_client_text_size("abc", 3U) == 3 &&
               squid_client_text_size("abc", 2U) == SQUID_CLIENT_ERROR_ARGUMENT,
               "Z80 text helper enforces counted-string limits")) {
        return 0;
    }

    client->packet[1] = 1U;
    set_response(ok_reply, sizeof(ok_reply));
    if (!check(squid_client_response(
                   client, 1U, 1U, 3U, &response_size) == 0 &&
               response_size == 3,
               "Z80 response helper validates a successful response")) {
        return 0;
    }
    client->packet[1] = 1U;
    set_response(status_reply, sizeof(status_reply));
    if (!check(squid_client_response(client, 1U, 1U, 2U, 0) == 7,
               "Z80 response helper returns plugin status")) {
        return 0;
    }
    client->packet[1] = 1U;
    set_response(bad_reply, sizeof(bad_reply));
    if (!check(squid_client_response(client, 1U, 1U, 2U, 0) ==
                   SQUID_CLIENT_ERROR_PROTOCOL,
               "Z80 response helper rejects a wrong opcode")) {
        return 0;
    }
    return 1;
}

int main(void)
{
    struct squid_client client;
    struct squid_client_bytes bytes;
    struct squid_client_time time_value;
    uint8_t workspace[SQUID_CLIENT_WORKSPACE_SIZE];
    uint16_t idle_count = 0U;
    static const uint8_t exchange_reply[] = { 0x81U, 0U, 0x34U, 0x12U };
    static const uint8_t time_reply[] = {
        (uint8_t)(SQUID_TIME_OP_GET | 0x80U), 0U,
        0xeaU, 0x07U, 8U, 25U, 21U, 4U, 33U, 2U,
        0x3cU, 0U, 0x78U, 0x56U, 0x34U, 0x12U
    };
    static const uint8_t system_reply[] = { 'z', '8', '0' };

    squid_client_init(
        &client, 3, workspace, SQUID_CLIENT_PACKET_MAX, idle, &idle_count);
    if (!check(client.socket_fd == 3 && client.packet == workspace &&
               client.packet_capacity == SQUID_CLIENT_PACKET_MAX &&
               client.idle == idle && client.idle_context == &idle_count,
               "Z80 initializer stores every field")) {
        return 1;
    }
    if (!test_internal_assembly(&client)) {
        return 1;
    }

    workspace[1] = 0x21U;
    workspace[2] = 0x43U;
    workspace[3] = 0x65U;
    set_response(exchange_reply, sizeof(exchange_reply));
    if (!check(squid_client_exchange(&client, 3U) == 4,
               "fragmented exchange succeeds") ||
        !check(sent_size == 4U && sent[0] == 3U && sent[1] == 0x21U &&
               sent[2] == 0x43U && sent[3] == 0x65U,
               "exchange adds one length byte") ||
        !check(memcmp(workspace, exchange_reply, sizeof(exchange_reply)) == 0,
               "exchange assembles the complete reply") ||
        !check(idle_count > 0U, "exchange runs the idle callback")) {
        return 1;
    }

    set_response(time_reply, sizeof(time_reply));
    if (!check(squid_client_time_get(
                   &client, SQUID_TIME_MODE_LOCAL, &time_value) == 0,
               "typed time request succeeds") ||
        !check(sent_size == 3U && sent[0] == 2U &&
               sent[1] == SQUID_TIME_OP_GET &&
               sent[2] == SQUID_TIME_MODE_LOCAL,
               "typed time request has minimal wire form") ||
        !check(time_value.year == 2026U && time_value.month == 8U &&
               time_value.day == 25U && time_value.hour == 21U &&
               time_value.utc_offset_minutes == 60 &&
               time_value.unix_seconds == 0x12345678UL,
               "typed time reply is decoded")) {
        return 1;
    }

    set_response(system_reply, sizeof(system_reply));
    if (!check(squid_client_system_id(&client, &bytes) == 0 &&
               bytes.size == sizeof(system_reply) &&
               memcmp(bytes.data, system_reply, sizeof(system_reply)) == 0,
               "system helper hides the text command")) {
        return 1;
    }

    squid_client_init(&client, 3, workspace, 2U, idle, &idle_count);
    set_response(exchange_reply, sizeof(exchange_reply));
    if (!check(
            squid_client_exchange(&client, 1U) == SQUID_CLIENT_ERROR_OVERFLOW,
            "oversized response is reported") ||
        !check(incoming_position == incoming_size,
               "oversized response is drained")) {
        return 1;
    }
    squid_client_init(
        &client, 3, workspace, SQUID_CLIENT_PACKET_MAX, idle, &idle_count);

    link_up = 0U;
    if (!check(squid_client_exchange(&client, 1U) == SQUID_CLIENT_ERROR_LINK,
               "down link is reported")) {
        return 1;
    }
    link_up = 1U;
    fail_send = 1U;
    if (!check(squid_client_exchange(&client, 1U) == SQUID_CLIENT_ERROR_IO,
               "send failure is reported")) {
        return 1;
    }
    fail_send = 0U;
    client.idle = cancel;
    incoming_size = 0U;
    incoming_position = 0U;
    if (!check(
            squid_client_exchange(&client, 1U) == SQUID_CLIENT_ERROR_CANCELLED,
            "idle callback can cancel a wait")) {
        return 1;
    }

    client.idle = 0;
    link_up = 0U;
    if (!check(exercise_complete_public_api(&client) < 0,
               "every public plugin call links with the Z80 ABI")) {
        return 1;
    }

    puts("z80 client scenarios: OK");
    return 0;
}
