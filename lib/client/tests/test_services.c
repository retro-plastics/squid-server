#include "squid_client/client.h"
#include "squid_server/filesystem_protocol.h"
#include "squid_server/retrovault_protocol.h"
#include "squid_server/tcp_proxy_protocol.h"
#include "squid_server/time_protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum fake_service {
    fake_echo,
    fake_system,
    fake_time,
    fake_filesystem,
    fake_retro,
    fake_tcp
};

static enum fake_service service;
static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static int fake_filesystem_exchange(uint8_t *packet, uint16_t request_size)
{
    uint8_t opcode = packet[1];
    switch (opcode) {
    case SQUID_FS_OP_STAT:
        CHECK((request_size == 3U) && (packet[2] == 1U) &&
            (packet[3] == (uint8_t)'x'));
        packet[0] = SQUID_FS_OP_STAT | SQUID_FS_RESPONSE_BIT;
        packet[1] = SQUID_FS_STATUS_OK;
        packet[2] = SQUID_FS_TYPE_FILE;
        put_u16(packet + 3U, 0644U);
        put_u32(packet + 5U, 1234U);
        put_u32(packet + 9U, 5678U);
        return 13;
    case SQUID_FS_OP_LIST:
        CHECK((request_size == 4U) && (packet[2] == 0U) &&
            (packet[3] == 0U) && (packet[4] == 0U));
        packet[0] = SQUID_FS_OP_LIST | SQUID_FS_RESPONSE_BIT;
        packet[1] = SQUID_FS_STATUS_OK;
        put_u16(packet + 2U, SQUID_FS_CURSOR_END);
        packet[4] = 1U;
        packet[5] = SQUID_FS_TYPE_FILE;
        packet[6] = 1U;
        packet[7] = (uint8_t)'x';
        put_u32(packet + 8U, 1234U);
        return 12;
    case SQUID_FS_OP_READ:
        CHECK((request_size == 8U) && (packet[2] == 9U) &&
            (packet[3] == 0U) && (packet[4] == 0U) &&
            (packet[5] == 0U) && (packet[6] == 3U) &&
            (packet[7] == 1U) && (packet[8] == (uint8_t)'x'));
        packet[0] = SQUID_FS_OP_READ | SQUID_FS_RESPONSE_BIT;
        packet[1] = SQUID_FS_STATUS_OK;
        put_u32(packet + 2U, 9U);
        put_u32(packet + 6U, 12U);
        packet[10] = 3U;
        memcpy(packet + 11U, "abc", 3U);
        return 14;
    case SQUID_FS_OP_WRITE:
        CHECK((request_size == 12U) && (packet[2] == 9U) &&
            (packet[3] == 0U) && (packet[4] == 0U) &&
            (packet[5] == 0U) && (packet[6] == SQUID_FS_WRITE_CREATE) &&
            (packet[7] == 1U) && (packet[8] == (uint8_t)'x') &&
            (packet[9] == 3U) && (memcmp(packet + 10U, "abc", 3U) == 0));
        packet[0] = SQUID_FS_OP_WRITE | SQUID_FS_RESPONSE_BIT;
        packet[1] = SQUID_FS_STATUS_OK;
        put_u32(packet + 2U, 9U);
        packet[6] = 3U;
        return 7;
    case SQUID_FS_OP_MKDIR:
    case SQUID_FS_OP_DELETE:
        CHECK((request_size == 3U) && (packet[2] == 1U));
        packet[0] = opcode | SQUID_FS_RESPONSE_BIT;
        packet[1] = SQUID_FS_STATUS_OK;
        return 2;
    case SQUID_FS_OP_RENAME:
        CHECK((request_size == 5U) && (packet[2] == 1U) &&
            (packet[3] == (uint8_t)'a') && (packet[4] == 1U) &&
            (packet[5] == (uint8_t)'b'));
        packet[0] = SQUID_FS_OP_RENAME | SQUID_FS_RESPONSE_BIT;
        packet[1] = SQUID_FS_STATUS_OK;
        return 2;
    default:
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
}

static int fake_retro_exchange(uint8_t *packet, uint16_t request_size)
{
    uint8_t opcode = packet[1];
    switch (opcode) {
    case RETRO_VAULT_OP_LIST:
        CHECK((request_size == 9U) && (packet[2] == 0U) &&
            (packet[3] == 0U) && (packet[4] == 3U) &&
            (memcmp(packet + 5U, "idp", 3U) == 0) &&
            (packet[8] == 1U) && (packet[9] == (uint8_t)'p'));
        packet[0] = opcode | RETRO_VAULT_RESPONSE_BIT;
        packet[1] = RETRO_VAULT_STATUS_OK;
        put_u16(packet + 2U, RETRO_VAULT_CURSOR_END);
        packet[4] = 1U;
        packet[5] = 1U;
        packet[6] = (uint8_t)'p';
        packet[7] = 1U;
        packet[8] = (uint8_t)'P';
        return 9;
    case RETRO_VAULT_OP_SEARCH:
        CHECK((request_size == 15U) && (packet[2] == 0U) &&
            (packet[3] == 0U) && (packet[4] == 3U) &&
            (memcmp(packet + 5U, "idp", 3U) == 0) &&
            (packet[8] == 5U) &&
            (memcmp(packet + 9U, "miner", 5U) == 0) &&
            (packet[14] == 1U) && (packet[15] == (uint8_t)'p'));
        packet[0] = opcode | RETRO_VAULT_RESPONSE_BIT;
        packet[1] = RETRO_VAULT_STATUS_OK;
        put_u16(packet + 2U, RETRO_VAULT_CURSOR_END);
        packet[4] = 1U;
        packet[5] = 1U;
        packet[6] = (uint8_t)'p';
        packet[7] = 1U;
        packet[8] = (uint8_t)'P';
        return 9;
    case RETRO_VAULT_OP_INFO:
        CHECK((request_size == 5U) && (packet[4] == 1U) &&
            (packet[5] == (uint8_t)'p'));
        packet[0] = RETRO_VAULT_OP_INFO | RETRO_VAULT_RESPONSE_BIT;
        packet[1] = RETRO_VAULT_STATUS_OK;
        put_u16(packet + 2U, RETRO_VAULT_CURSOR_END);
        packet[4] = 1U;
        packet[5] = RETRO_VAULT_INFO_NAME;
        packet[6] = 1U;
        packet[7] = (uint8_t)'P';
        return 8;
    case RETRO_VAULT_OP_DOWNLOAD:
        CHECK((request_size == 10U) && (packet[2] == 4U) &&
            (packet[3] == 0U) && (packet[4] == 0U) &&
            (packet[5] == 0U) && (packet[6] == 2U) &&
            (packet[7] == 1U) && (packet[8] == (uint8_t)'p') &&
            (packet[9] == 1U) && (packet[10] == (uint8_t)'d'));
        packet[0] = RETRO_VAULT_OP_DOWNLOAD | RETRO_VAULT_RESPONSE_BIT;
        packet[1] = RETRO_VAULT_STATUS_OK;
        put_u32(packet + 2U, 4U);
        put_u32(packet + 6U, 6U);
        packet[10] = 2U;
        packet[11] = 0xaaU;
        packet[12] = 0xbbU;
        return 13;
    default:
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
}

static int fake_tcp_exchange(uint8_t *packet, uint16_t request_size)
{
    uint8_t opcode = packet[1];
    switch (opcode) {
    case SQUID_TCP_OP_CONNECT:
        CHECK((request_size == 8U) && (packet[2] == 80U) &&
            (packet[3] == 0U) && (packet[4] == 4U) &&
            (memcmp(packet + 5U, "host", 4U) == 0));
        packet[0] = SQUID_TCP_OP_CONNECT | SQUID_TCP_RESPONSE_BIT;
        packet[1] = SQUID_TCP_STATUS_OK;
        packet[2] = SQUID_TCP_FAMILY_IPV4;
        return 3;
    case SQUID_TCP_OP_WRITE:
        CHECK((request_size == 5U) && (packet[2] == 3U) &&
            (memcmp(packet + 3U, "abc", 3U) == 0));
        packet[0] = SQUID_TCP_OP_WRITE | SQUID_TCP_RESPONSE_BIT;
        packet[1] = SQUID_TCP_STATUS_OK;
        packet[2] = 3U;
        return 3;
    case SQUID_TCP_OP_READ:
        CHECK((request_size == 4U) && (packet[2] == 0U) &&
            (packet[3] == 0U) && (packet[4] == 2U));
        packet[0] = SQUID_TCP_OP_READ | SQUID_TCP_RESPONSE_BIT;
        packet[1] = SQUID_TCP_STATUS_OK;
        packet[2] = SQUID_TCP_READ_EOF;
        packet[3] = 2U;
        packet[4] = 0xaaU;
        packet[5] = 0xbbU;
        return 6;
    case SQUID_TCP_OP_CLOSE:
        packet[0] = SQUID_TCP_OP_CLOSE | SQUID_TCP_RESPONSE_BIT;
        packet[1] = SQUID_TCP_STATUS_OK;
        return 2;
    case SQUID_TCP_OP_STATUS:
        packet[0] = SQUID_TCP_OP_STATUS | SQUID_TCP_RESPONSE_BIT;
        packet[1] = SQUID_TCP_STATUS_OK;
        packet[2] = 1U;
        return 3;
    default:
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
}

int squid_client_exchange(struct squid_client *client, uint16_t request_size)
{
    uint8_t *packet = client->packet;

    switch (service) {
    case fake_echo:
        memmove(packet, packet + 1U, request_size);
        return (int)request_size;
    case fake_system:
        CHECK((request_size == 2U) && (packet[1] == (uint8_t)'i') &&
            (packet[2] == (uint8_t)'d'));
        memcpy(packet, "host", 4U);
        return 4;
    case fake_time:
        CHECK((request_size == 2U) && (packet[1] == SQUID_TIME_OP_GET) &&
            (packet[2] == SQUID_TIME_MODE_LOCAL));
        packet[0] = SQUID_TIME_OP_GET | SQUID_TIME_RESPONSE_BIT;
        packet[1] = SQUID_TIME_STATUS_OK;
        put_u16(packet + 2U, 2026U);
        packet[4] = 8U; packet[5] = 25U; packet[6] = 12U;
        packet[7] = 34U; packet[8] = 56U;
        packet[9] = (uint8_t)(2U | SQUID_TIME_DST_BIT);
        put_u16(packet + 10U, 60U);
        put_u32(packet + 12U, 123456U);
        return SQUID_TIME_REPLY_SIZE;
    case fake_filesystem:
        return fake_filesystem_exchange(packet, request_size);
    case fake_retro:
        return fake_retro_exchange(packet, request_size);
    case fake_tcp:
        return fake_tcp_exchange(packet, request_size);
    }
    return SQUID_CLIENT_ERROR_PROTOCOL;
}

static void test_simple(struct squid_client *client)
{
    struct squid_client_bytes bytes;
    struct squid_client_time time_value;

    service = fake_echo;
    CHECK(squid_client_echo(client, "hello", 5U, &bytes) == 0);
    CHECK((bytes.size == 5U) && (memcmp(bytes.data, "hello", 5U) == 0));

    service = fake_system;
    CHECK(squid_client_system_id(client, &bytes) == 0);
    CHECK((bytes.size == 4U) && (memcmp(bytes.data, "host", 4U) == 0));

    service = fake_time;
    CHECK(squid_client_time_get(client, SQUID_TIME_MODE_LOCAL, &time_value) == 0);
    CHECK((time_value.year == 2026U) && (time_value.month == 8U) &&
        (time_value.weekday == 2U) && (time_value.daylight_saving == 1U) &&
        (time_value.utc_offset_minutes == 60) &&
        (time_value.unix_seconds == 123456U));
}

static void test_filesystem(struct squid_client *client)
{
    struct squid_client_fs_stat stat_value;
    struct squid_client_fs_list page;
    struct squid_client_fs_entry entry;
    struct squid_client_fs_read chunk;
    uint8_t written = 0U;

    service = fake_filesystem;
    CHECK(squid_client_fs_stat(client, "x", &stat_value) == 0);
    CHECK((stat_value.type == SQUID_FS_TYPE_FILE) &&
        (stat_value.mode == 0644U) && (stat_value.size == 1234U));
    CHECK(squid_client_fs_list(client, "", 0U, &page) == 0);
    CHECK(squid_client_fs_list_next(&page, &entry) == 1);
    CHECK((entry.name.size == 1U) && (entry.name.data[0] == (uint8_t)'x') &&
        (entry.size == 1234U));
    CHECK(squid_client_fs_list_next(&page, &entry) == 0);
    CHECK(squid_client_fs_read(client, "x", 9U, 3U, &chunk) == 0);
    CHECK((chunk.offset == 9U) && (chunk.total_size == 12U) &&
        (chunk.data.size == 3U) && (memcmp(chunk.data.data, "abc", 3U) == 0));
    CHECK(squid_client_fs_write(
        client, "x", 9U, SQUID_FS_WRITE_CREATE, "abc", 3U, &written) == 0);
    CHECK(written == 3U);
    CHECK(squid_client_fs_mkdir(client, "x") == 0);
    CHECK(squid_client_fs_delete(client, "x") == 0);
    CHECK(squid_client_fs_rename(client, "a", "b") == 0);
}

static void test_retro(struct squid_client *client)
{
    struct squid_client_retro_list page;
    struct squid_client_retro_entry entry;
    struct squid_client_retro_info info;
    struct squid_client_retro_value value;
    struct squid_client_retro_download chunk;

    service = fake_retro;
    CHECK(squid_client_retro_list(client, "idp", "p", 0U, &page) == 0);
    CHECK(squid_client_retro_list_next(&page, &entry) == 1);
    CHECK((entry.id.data[0] == (uint8_t)'p') &&
        (entry.name.data[0] == (uint8_t)'P'));
    CHECK(squid_client_retro_search(
        client, "idp", "p", "miner", 0U, &page) == 0);
    CHECK(squid_client_retro_info(client, "p", 0U, &info) == 0);
    CHECK(squid_client_retro_info_next(&info, &value) == 1);
    CHECK((value.type == RETRO_VAULT_INFO_NAME) &&
        (value.value.data[0] == (uint8_t)'P'));
    CHECK(squid_client_retro_download(client, "p", "d", 4U, 2U, &chunk) == 0);
    CHECK((chunk.offset == 4U) && (chunk.total_size == 6U) &&
        (chunk.data.size == 2U) && (chunk.data.data[1] == 0xbbU));
}

static void test_tcp(struct squid_client *client)
{
    struct squid_client_tcp_read chunk;
    uint8_t value = 0U;

    service = fake_tcp;
    CHECK(squid_client_tcp_connect(client, "host", 80U, &value) == 0);
    CHECK(value == SQUID_TCP_FAMILY_IPV4);
    CHECK(squid_client_tcp_write(client, "abc", 3U, &value) == 0);
    CHECK(value == 3U);
    CHECK(squid_client_tcp_read(client, 0U, 2U, &chunk) == 0);
    CHECK((chunk.eof == 1U) && (chunk.data.size == 2U) &&
        (chunk.data.data[0] == 0xaaU));
    CHECK(squid_client_tcp_status(client, &value) == 0);
    CHECK(value == 1U);
    CHECK(squid_client_tcp_close(client) == 0);
}

int main(void)
{
    uint8_t workspace[SQUID_CLIENT_WORKSPACE_SIZE];
    struct squid_client client = { 1, workspace, SQUID_CLIENT_PACKET_MAX, 0, 0 };

    test_simple(&client);
    test_filesystem(&client);
    test_retro(&client);
    test_tcp(&client);

    if (failures != 0) {
        fprintf(stderr, "%d squid client checks failed\n", failures);
        return 1;
    }
    puts("squid client service helpers: OK");
    return 0;
}
