#include "client_internal.h"
#include "squid_client/filesystem.h"

static int filesystem_path_size(const char *path, int allow_empty)
{
    int size = squid_client_text_size(path, SQUID_FS_PATH_MAX);
    if ((size < 0) || (!allow_empty && (size == 0))) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    return size;
}

static int filesystem_path_call(
    struct squid_client *client,
    uint8_t opcode,
    const char *path,
    int allow_empty,
    uint8_t minimum_response_size,
    int *response_size
)
{
    uint8_t *request = 0;
    int path_size = filesystem_path_size(path, allow_empty);
    uint16_t request_size = 0U;

    if ((client == 0) || (client->packet == 0) || (path_size < 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(2U + (uint16_t)path_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = opcode;
    request[1] = (uint8_t)path_size;
    squid_client_copy(request + 2U, path, (uint16_t)path_size);
    return squid_client_response(
        client,
        request_size,
        opcode,
        minimum_response_size,
        response_size
    );
}

int squid_client_fs_stat(
    struct squid_client *client,
    const char *path,
    struct squid_client_fs_stat *value
)
{
    const uint8_t *response = 0;
    int size = 0;

    if (value == 0) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    {
        int result = filesystem_path_call(
            client,
            SQUID_FS_OP_STAT,
            path,
            1,
            13U,
            &size
        );
        if (result != 0) {
            return result;
        }
    }
    if (size != 13) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    response = client->packet;
    value->type = response[2];
    value->mode = squid_client_read_u16(response + 3U);
    value->size = squid_client_read_u32(response + 5U);
    value->modification_time = squid_client_read_u32(response + 9U);
    return 0;
}

int squid_client_fs_list(
    struct squid_client *client,
    const char *path,
    uint16_t cursor,
    struct squid_client_fs_list *page
)
{
    uint8_t *request = 0;
    int path_size = filesystem_path_size(path, 1);
    uint16_t request_size = 0U;
    int size = 0;

    if ((client == 0) || (client->packet == 0) || (page == 0) ||
        (path_size < 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(4U + (uint16_t)path_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_FS_OP_LIST;
    squid_client_write_u16(request + 1U, cursor);
    request[3] = (uint8_t)path_size;
    squid_client_copy(request + 4U, path, (uint16_t)path_size);
    {
        int result = squid_client_response(
            client, request_size, SQUID_FS_OP_LIST, 5U, &size);
        if (result != 0) {
            return result;
        }
    }
    page->next_cursor = squid_client_read_u16(client->packet + 2U);
    page->entries_left = client->packet[4];
    page->next = client->packet + 5U;
    page->end = client->packet + size;
    return 0;
}

int squid_client_fs_list_next(
    struct squid_client_fs_list *page,
    struct squid_client_fs_entry *entry
)
{
    const uint8_t *current = 0;
    uint8_t name_size = 0U;

    if ((page == 0) || (entry == 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    if (page->entries_left == 0U) {
        return page->next == page->end ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
    }
    current = page->next;
    if ((current == 0) || (page->end == 0) ||
        (current > page->end) || ((uint16_t)(page->end - current) < 6U)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    name_size = current[1];
    if ((uint16_t)(page->end - current) < (uint16_t)(6U + name_size)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    entry->type = current[0];
    entry->name.data = current + 2U;
    entry->name.size = name_size;
    entry->size = squid_client_read_u32(current + 2U + name_size);
    page->next = current + 6U + name_size;
    --page->entries_left;
    return 1;
}

int squid_client_fs_read(
    struct squid_client *client,
    const char *path,
    uint32_t offset,
    uint8_t maximum_bytes,
    struct squid_client_fs_read *chunk
)
{
    uint8_t *request = 0;
    uint8_t data_size = 0U;
    int path_size = filesystem_path_size(path, 0);
    uint16_t request_size = 0U;
    int size = 0;

    if ((client == 0) || (client->packet == 0) || (chunk == 0) ||
        (path_size < 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(7U + (uint16_t)path_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_FS_OP_READ;
    squid_client_write_u32(request + 1U, offset);
    request[5] = maximum_bytes;
    request[6] = (uint8_t)path_size;
    squid_client_copy(request + 7U, path, (uint16_t)path_size);
    {
        int result = squid_client_response(
            client, request_size, SQUID_FS_OP_READ, 11U, &size);
        if (result != 0) {
            return result;
        }
    }
    data_size = client->packet[10];
    if (size != (int)(11U + data_size)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    chunk->offset = squid_client_read_u32(client->packet + 2U);
    chunk->total_size = squid_client_read_u32(client->packet + 6U);
    chunk->data.data = client->packet + 11U;
    chunk->data.size = data_size;
    return 0;
}

int squid_client_fs_write(
    struct squid_client *client,
    const char *path,
    uint32_t offset,
    uint8_t flags,
    const void *data,
    uint8_t size,
    uint8_t *written
)
{
    uint8_t *request = 0;
    int path_size = filesystem_path_size(path, 0);
    uint16_t request_size = 0U;
    int response_size = 0;

    if ((client == 0) || (client->packet == 0) || (written == 0) ||
        (path_size < 0) ||
        ((size > 0U) && (data == 0))) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(8U + (uint16_t)path_size + size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_FS_OP_WRITE;
    squid_client_write_u32(request + 1U, offset);
    request[5] = flags;
    request[6] = (uint8_t)path_size;
    squid_client_copy(request + 7U, path, (uint16_t)path_size);
    request[7U + path_size] = size;
    squid_client_copy(request + 8U + path_size, data, size);
    {
        int result = squid_client_response(
            client,
            request_size,
            SQUID_FS_OP_WRITE,
            7U,
            &response_size
        );
        if (result != 0) {
            return result;
        }
    }
    if ((response_size != 7) || (client->packet[6] > size) ||
        (squid_client_read_u32(client->packet + 2U) != offset)) {
        return SQUID_CLIENT_ERROR_PROTOCOL;
    }
    *written = client->packet[6];
    return 0;
}

int squid_client_fs_mkdir(struct squid_client *client, const char *path)
{
    int response_size = 0;
    int result = filesystem_path_call(
        client, SQUID_FS_OP_MKDIR, path, 0, 2U, &response_size);
    if (result != 0) {
        return result;
    }
    return response_size == 2 ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
}

int squid_client_fs_delete(struct squid_client *client, const char *path)
{
    int response_size = 0;
    int result = filesystem_path_call(
        client, SQUID_FS_OP_DELETE, path, 0, 2U, &response_size);
    if (result != 0) {
        return result;
    }
    return response_size == 2 ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
}

int squid_client_fs_rename(
    struct squid_client *client,
    const char *old_path,
    const char *new_path
)
{
    uint8_t *request = 0;
    int old_size = filesystem_path_size(old_path, 0);
    int new_size = filesystem_path_size(new_path, 0);
    uint16_t request_size = 0U;
    int response_size = 0;
    int result = 0;
    if ((client == 0) || (client->packet == 0) ||
        (old_size < 0) || (new_size < 0)) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request_size = (uint16_t)(3U + (uint16_t)old_size + (uint16_t)new_size);
    if (request_size > client->packet_capacity) {
        return SQUID_CLIENT_ERROR_ARGUMENT;
    }
    request = client->packet + 1U;
    request[0] = SQUID_FS_OP_RENAME;
    request[1] = (uint8_t)old_size;
    squid_client_copy(request + 2U, old_path, (uint16_t)old_size);
    request[2U + old_size] = (uint8_t)new_size;
    squid_client_copy(request + 3U + old_size, new_path, (uint16_t)new_size);
    result = squid_client_response(
        client,
        request_size,
        SQUID_FS_OP_RENAME,
        2U,
        &response_size
    );
    if (result != 0) {
        return result;
    }
    return response_size == 2 ? 0 : SQUID_CLIENT_ERROR_PROTOCOL;
}
