#include "client_internal.h"
#include "squid_client/retrovault.h"

static int retro_text_size(const char *text)
{
    return squid_client_text_size(text, 255U);
}

static int retro_page_response(
    struct squid_client *client,
    uint16_t request_size,
    uint8_t opcode,
    struct squid_client_retro_list *page
)
{
    int size = 0;
    int result = squid_client_response(
        client, request_size, opcode, 5U, &size);
    if (result != 0) {
        return result;
    }
    page->next_cursor = squid_client_read_u16(client->packet + 2U);
    page->entries_left = client->packet[4];
    page->next = client->packet + 5U;
    page->end = client->packet + size;
    return 0;
}

int squid_client_retro_list(
    struct squid_client *client,
    const char *platform,
    const char *model,
    uint16_t cursor,
    struct squid_client_retro_list *page
)
{
    uint8_t *request = 0;
    int platform_size = retro_text_size(platform);
    int model_size = retro_text_size(model);
    uint16_t request_size = 0U;
    uint16_t position = 0U;

    if ((client == 0) || (client->packet == 0) || (page == 0) ||
        (platform_size < 0) || (model_size < 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(4U + (uint16_t)platform_size);
    if (model_size > 0) {
        request_size = (uint16_t)(request_size + 1U + (uint16_t)model_size);
    }
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = RETRO_VAULT_OP_LIST;
    squid_client_write_u16(request + 1U, cursor);
    request[3] = (uint8_t)platform_size;
    squid_client_copy(request + 4U, platform, (uint16_t)platform_size);
    position = (uint16_t)(4U + (uint16_t)platform_size);
    if (model_size > 0) {
        request[position++] = (uint8_t)model_size;
        squid_client_copy(request + position, model, (uint16_t)model_size);
    }
    return retro_page_response(
        client, request_size, RETRO_VAULT_OP_LIST, page);
}

int squid_client_retro_search(
    struct squid_client *client,
    const char *platform,
    const char *model,
    const char *query,
    uint16_t cursor,
    struct squid_client_retro_list *page
)
{
    uint8_t *request = 0;
    int platform_size = retro_text_size(platform);
    int model_size = retro_text_size(model);
    int query_size = retro_text_size(query);
    uint16_t request_size = 0U;
    uint16_t position = 0U;

    if ((client == 0) || (client->packet == 0) || (page == 0) ||
        (platform_size < 0) || (model_size < 0) || (query_size < 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(5U + (uint16_t)platform_size +
        (uint16_t)query_size);
    if (model_size > 0) {
        request_size = (uint16_t)(request_size + 1U + (uint16_t)model_size);
    }
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = RETRO_VAULT_OP_SEARCH;
    squid_client_write_u16(request + 1U, cursor);
    request[3] = (uint8_t)platform_size;
    squid_client_copy(request + 4U, platform, (uint16_t)platform_size);
    position = (uint16_t)(4U + (uint16_t)platform_size);
    request[position++] = (uint8_t)query_size;
    squid_client_copy(request + position, query, (uint16_t)query_size);
    position = (uint16_t)(position + (uint16_t)query_size);
    if (model_size > 0) {
        request[position++] = (uint8_t)model_size;
        squid_client_copy(request + position, model, (uint16_t)model_size);
    }
    return retro_page_response(
        client, request_size, RETRO_VAULT_OP_SEARCH, page);
}

int squid_client_retro_list_next(
    struct squid_client_retro_list *page,
    struct squid_client_retro_entry *entry
)
{
    const uint8_t *current = 0;
    uint8_t id_size = 0U;
    uint8_t name_size = 0U;
    uint16_t needed = 0U;

    if ((page == 0) || (entry == 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    if (page->entries_left == 0U) {
        return page->next == page->end ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
    }
    current = page->next;
    if ((current == 0) || (page->end == 0) || (current >= page->end)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    id_size = current[0];
    needed = (uint16_t)(2U + id_size);
    if ((uint16_t)(page->end - current) < needed) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    name_size = current[1U + id_size];
    needed = (uint16_t)(needed + name_size);
    if ((uint16_t)(page->end - current) < needed) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    entry->id.data = current + 1U;
    entry->id.size = id_size;
    entry->name.data = current + 2U + id_size;
    entry->name.size = name_size;
    page->next = current + needed;
    --page->entries_left;
    return 1;
}

int squid_client_retro_info(
    struct squid_client *client,
    const char *package_id,
    uint16_t cursor,
    struct squid_client_retro_info *page
)
{
    uint8_t *request = 0;
    int id_size = retro_text_size(package_id);
    uint16_t request_size = 0U;
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) || (page == 0) ||
        (id_size <= 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(4U + (uint16_t)id_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = RETRO_VAULT_OP_INFO;
    squid_client_write_u16(request + 1U, cursor);
    request[3] = (uint8_t)id_size;
    squid_client_copy(request + 4U, package_id, (uint16_t)id_size);
    result = squid_client_response(
        client, request_size, RETRO_VAULT_OP_INFO, 5U, &response_size);
    if (result != 0) {
        return result;
    }
    page->next_cursor = squid_client_read_u16(client->packet + 2U);
    page->values_left = client->packet[4];
    page->next = client->packet + 5U;
    page->end = client->packet + response_size;
    return 0;
}

int squid_client_retro_info_next(
    struct squid_client_retro_info *page,
    struct squid_client_retro_value *value
)
{
    const uint8_t *current = 0;
    uint8_t value_size = 0U;

    if ((page == 0) || (value == 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    if (page->values_left == 0U) {
        return page->next == page->end ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
    }
    current = page->next;
    if ((current == 0) || (page->end == 0) ||
        (current > page->end) ||
        ((uint16_t)(page->end - current) < 2U)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    value_size = current[1];
    if ((uint16_t)(page->end - current) < (uint16_t)(2U + value_size)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    value->type = current[0];
    value->value.data = current + 2U;
    value->value.size = value_size;
    page->next = current + 2U + value_size;
    --page->values_left;
    return 1;
}

int squid_client_retro_download(
    struct squid_client *client,
    const char *package_id,
    const char *download_id,
    uint32_t offset,
    uint8_t maximum_bytes,
    struct squid_client_retro_download *chunk
)
{
    uint8_t *request = 0;
    int package_size = retro_text_size(package_id);
    int download_size = retro_text_size(download_id);
    uint16_t request_size = 0U;
    uint16_t position = 0U;
    uint8_t data_size = 0U;
    int response_size = 0;
    int result = 0;

    if ((client == 0) || (client->packet == 0) || (chunk == 0) ||
        (package_size <= 0) || (download_size <= 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(8U + (uint16_t)package_size +
        (uint16_t)download_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = RETRO_VAULT_OP_DOWNLOAD;
    squid_client_write_u32(request + 1U, offset);
    request[5] = maximum_bytes;
    request[6] = (uint8_t)package_size;
    squid_client_copy(request + 7U, package_id, (uint16_t)package_size);
    position = (uint16_t)(7U + (uint16_t)package_size);
    request[position++] = (uint8_t)download_size;
    squid_client_copy(request + position, download_id, (uint16_t)download_size);
    result = squid_client_response(
        client, request_size, RETRO_VAULT_OP_DOWNLOAD, 11U, &response_size);
    if (result != 0) {
        return result;
    }
    data_size = client->packet[10];
    if (response_size != (int)(11U + data_size)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    chunk->offset = squid_client_read_u32(client->packet + 2U);
    chunk->total_size = squid_client_read_u32(client->packet + 6U);
    chunk->data.data = client->packet + 11U;
    chunk->data.size = data_size;
    return 0;
}
