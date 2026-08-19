#define _POSIX_C_SOURCE 200809L

#include "filesystem_core.h"
#include "squid_server/filesystem_protocol.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8U));
}

static uint32_t read_u32(const uint8_t *input)
{
    return (uint32_t)input[0] |
        ((uint32_t)input[1] << 8U) |
        ((uint32_t)input[2] << 16U) |
        ((uint32_t)input[3] << 24U);
}

static size_t make_path_request(uint8_t opcode, const char *path, uint8_t *request)
{
    size_t path_size = strlen(path);

    request[0] = opcode;
    request[1] = (uint8_t)path_size;
    memcpy(request + 2U, path, path_size);
    return path_size + 2U;
}

static int test_capabilities(struct squid_fs_context *context)
{
    const uint8_t request[] = { SQUID_FS_OP_CAPABILITIES };
    uint8_t response[254];
    int size = handle_squid_fs_request(
        context, request, sizeof(request), response, sizeof(response));

    return (size == 9) && (response[1] == SQUID_FS_STATUS_OK) &&
        (response[2] == SQUID_FS_PROTOCOL_VERSION) &&
        ((read_u16(response + 3U) & SQUID_FS_FEATURE_WRITE) != 0U) &&
        (response[8] == 243U) ? 0 : 1;
}

static int test_stat_root(struct squid_fs_context *context)
{
    const uint8_t request[] = { SQUID_FS_OP_STAT, 0U };
    uint8_t response[32];
    int size = handle_squid_fs_request(
        context, request, sizeof(request), response, sizeof(response));

    return (size == 13) && (response[1] == SQUID_FS_STATUS_OK) &&
        (response[2] == SQUID_FS_TYPE_DIRECTORY) ? 0 : 1;
}

static int test_list(struct squid_fs_context *context)
{
    const uint8_t request[] = { SQUID_FS_OP_LIST, 0U, 0U, 0U };
    uint8_t response[254];
    size_t offset = 5U;
    uint8_t index = 0U;
    int saw_file = 0;
    int saw_directory = 0;
    int size = handle_squid_fs_request(
        context, request, sizeof(request), response, sizeof(response));

    if ((size < 5) || (response[1] != SQUID_FS_STATUS_OK) ||
        (read_u16(response + 2U) != SQUID_FS_CURSOR_END)) {
        return 1;
    }

    for (index = 0U; index < response[4]; ++index) {
        uint8_t type = 0U;
        uint8_t name_size = 0U;

        if (offset + 2U > (size_t)size) {
            return 1;
        }
        type = response[offset++];
        name_size = response[offset++];
        if ((size_t)name_size + 4U > (size_t)size - offset) {
            return 1;
        }
        if ((name_size == 9U) &&
            (memcmp(response + offset, "hello.txt", 9U) == 0) &&
            (type == SQUID_FS_TYPE_FILE) &&
            (read_u32(response + offset + name_size) == 5U)) {
            saw_file = 1;
        }
        if ((name_size == 3U) &&
            (memcmp(response + offset, "dir", 3U) == 0) &&
            (type == SQUID_FS_TYPE_DIRECTORY)) {
            saw_directory = 1;
        }
        offset += name_size + 4U;
    }

    return (saw_file && saw_directory && (offset == (size_t)size)) ? 0 : 1;
}

static int test_read(struct squid_fs_context *context)
{
    static const char path[] = "hello.txt";
    uint8_t request[7U + sizeof(path) - 1U];
    uint8_t response[32];
    int size = 0;

    request[0] = SQUID_FS_OP_READ;
    request[1] = 1U;
    request[2] = 0U;
    request[3] = 0U;
    request[4] = 0U;
    request[5] = 3U;
    request[6] = (uint8_t)(sizeof(path) - 1U);
    memcpy(request + 7U, path, sizeof(path) - 1U);

    size = handle_squid_fs_request(
        context, request, sizeof(request), response, sizeof(response));
    return (size == 14) && (response[1] == SQUID_FS_STATUS_OK) &&
        (read_u32(response + 2U) == 1U) &&
        (read_u32(response + 6U) == 5U) && (response[10] == 3U) &&
        (memcmp(response + 11U, "ell", 3U) == 0) ? 0 : 1;
}

static int test_mutation(struct squid_fs_context *context)
{
    static const char path[] = "new.bin";
    static const uint8_t data[] = { 1U, 2U, 3U };
    uint8_t write_request[8U + sizeof(path) - 1U + sizeof(data)];
    uint8_t request[254];
    uint8_t response[254];
    size_t cursor = 7U;
    size_t request_size = 0U;
    int size = 0;

    write_request[0] = SQUID_FS_OP_WRITE;
    write_request[1] = 0U;
    write_request[2] = 0U;
    write_request[3] = 0U;
    write_request[4] = 0U;
    write_request[5] = SQUID_FS_WRITE_CREATE | SQUID_FS_WRITE_TRUNCATE;
    write_request[6] = (uint8_t)(sizeof(path) - 1U);
    memcpy(write_request + cursor, path, sizeof(path) - 1U);
    cursor += sizeof(path) - 1U;
    write_request[cursor++] = (uint8_t)sizeof(data);
    memcpy(write_request + cursor, data, sizeof(data));

    size = handle_squid_fs_request(
        context,
        write_request,
        sizeof(write_request),
        response,
        sizeof(response)
    );
    if ((size != 7) || (response[1] != SQUID_FS_STATUS_OK) ||
        (response[6] != sizeof(data))) {
        return 1;
    }

    request_size = make_path_request(SQUID_FS_OP_MKDIR, "created", request);
    size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));
    if ((size != 2) || (response[1] != SQUID_FS_STATUS_OK)) {
        return 1;
    }

    request[0] = SQUID_FS_OP_RENAME;
    request[1] = (uint8_t)strlen(path);
    memcpy(request + 2U, path, strlen(path));
    cursor = 2U + strlen(path);
    request[cursor++] = 17U;
    memcpy(request + cursor, "created/moved.bin", 17U);
    cursor += 17U;
    size = handle_squid_fs_request(
        context, request, cursor, response, sizeof(response));
    if ((size != 2) || (response[1] != SQUID_FS_STATUS_OK)) {
        return 1;
    }

    request_size = make_path_request(
        SQUID_FS_OP_STAT, "created/moved.bin", request);
    size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));
    if ((size != 13) || (response[2] != SQUID_FS_TYPE_FILE) ||
        (read_u32(response + 5U) != sizeof(data))) {
        return 1;
    }

    request_size = make_path_request(
        SQUID_FS_OP_DELETE, "created/moved.bin", request);
    size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));
    if ((size != 2) || (response[1] != SQUID_FS_STATUS_OK)) {
        return 1;
    }
    request_size = make_path_request(SQUID_FS_OP_DELETE, "created", request);
    size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));
    return (size == 2) && (response[1] == SQUID_FS_STATUS_OK) ? 0 : 1;
}

static int test_path_safety(struct squid_fs_context *context)
{
    uint8_t request[32];
    uint8_t response[16];
    size_t request_size = make_path_request(SQUID_FS_OP_STAT, "../etc", request);
    int size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));

    if ((size != 2) || (response[1] != SQUID_FS_STATUS_BAD_REQUEST)) {
        return 1;
    }

    request_size = make_path_request(SQUID_FS_OP_STAT, "/etc", request);
    size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));
    return (size == 2) &&
        (response[1] == SQUID_FS_STATUS_BAD_REQUEST) ? 0 : 1;
}

static int test_read_only(struct squid_fs_context *context)
{
    uint8_t request[32];
    uint8_t response[16];
    size_t request_size = 0U;
    int size = 0;

    context->read_only = 1;
    request_size = make_path_request(SQUID_FS_OP_DELETE, "hello.txt", request);
    size = handle_squid_fs_request(
        context, request, request_size, response, sizeof(response));
    context->read_only = 0;

    return (size == 2) && (response[1] == SQUID_FS_STATUS_READ_ONLY) ? 0 : 1;
}

int main(void)
{
    char root[] = "/tmp/squid-filesystem-test.XXXXXX";
    char file_path[sizeof(root) + 16U];
    char directory_path[sizeof(root) + 8U];
    struct squid_fs_context context = { -1, 0 };
    int file_fd = -1;
    int failed = 0;

    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(file_path, sizeof(file_path), "%s/hello.txt", root);
    snprintf(directory_path, sizeof(directory_path), "%s/dir", root);
    file_fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if ((file_fd < 0) || (write(file_fd, "hello", 5U) != 5)) {
        perror("filesystem test setup");
        if (file_fd >= 0) {
            close(file_fd);
        }
        unlink(file_path);
        rmdir(root);
        return 1;
    }
    if (close(file_fd) != 0) {
        perror("filesystem test close");
        unlink(file_path);
        rmdir(root);
        return 1;
    }
    file_fd = -1;
    if ((mkdir(directory_path, 0700) != 0) ||
        (init_squid_fs_context(&context, root, 0) != 0)) {
        perror("filesystem test setup");
        unlink(file_path);
        rmdir(directory_path);
        rmdir(root);
        return 1;
    }

#define RUN_TEST(test_call) \
    do { \
        if ((test_call) != 0) { \
            fprintf(stderr, "%s failed\n", #test_call); \
            failed = 1; \
        } \
    } while (0)

    RUN_TEST(test_capabilities(&context));
    RUN_TEST(test_stat_root(&context));
    RUN_TEST(test_list(&context));
    RUN_TEST(test_read(&context));
    RUN_TEST(test_mutation(&context));
    RUN_TEST(test_path_safety(&context));
    RUN_TEST(test_read_only(&context));

#undef RUN_TEST

    free_squid_fs_context(&context);
    unlink(file_path);
    rmdir(directory_path);
    rmdir(root);

    if (failed) {
        return 1;
    }
    puts("all filesystem tests passed");
    return 0;
}
