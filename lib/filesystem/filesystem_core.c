#define _POSIX_C_SOURCE 200809L

#include "filesystem_core.h"
#include "squid_server/filesystem_protocol.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define squid_fs_page_header_size 5U
#define squid_fs_read_header_size 11U

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

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

static uint8_t status_from_errno(int error_number)
{
    switch (error_number) {
    case ENOENT:
        return SQUID_FS_STATUS_NOT_FOUND;
    case ENOTDIR:
        return SQUID_FS_STATUS_NOT_DIRECTORY;
    case EISDIR:
        return SQUID_FS_STATUS_IS_DIRECTORY;
    case EACCES:
    case EPERM:
    case ELOOP:
        return SQUID_FS_STATUS_ACCESS_DENIED;
    case EEXIST:
        return SQUID_FS_STATUS_EXISTS;
    case ENAMETOOLONG:
    case EOVERFLOW:
    case EFBIG:
        return SQUID_FS_STATUS_TOO_LARGE;
    case ENOTEMPTY:
        return SQUID_FS_STATUS_NOT_EMPTY;
    case EROFS:
        return SQUID_FS_STATUS_READ_ONLY;
    case ENOSPC:
    case EDQUOT:
        return SQUID_FS_STATUS_NO_SPACE;
    default:
        return SQUID_FS_STATUS_IO_ERROR;
    }
}

static int write_response(
    uint8_t opcode,
    uint8_t status,
    uint8_t *response,
    size_t response_capacity
)
{
    if ((response == NULL) || (response_capacity < 2U)) {
        return -1;
    }

    response[0] = (uint8_t)(opcode | SQUID_FS_RESPONSE_BIT);
    response[1] = status;
    return 2;
}

static int path_is_valid(
    const uint8_t *input,
    size_t input_size,
    int allow_empty,
    char *path
)
{
    size_t index = 0U;
    size_t component_start = 0U;

    if ((input_size == 0U) && allow_empty) {
        path[0] = '\0';
        return 1;
    }
    if ((input == NULL) || (input_size == 0U) ||
        (input_size > SQUID_FS_PATH_MAX) || (input[0] == '/') ||
        (input[input_size - 1U] == '/')) {
        return 0;
    }

    for (index = 0U; index <= input_size; ++index) {
        if ((index < input_size) && (input[index] != '/')) {
            if (input[index] == '\0') {
                return 0;
            }
            path[index] = (char)input[index];
            continue;
        }

        if ((index == component_start) ||
            ((index - component_start == 1U) &&
             (input[component_start] == '.')) ||
            ((index - component_start == 2U) &&
             (input[component_start] == '.') &&
             (input[component_start + 1U] == '.'))) {
            return 0;
        }
        if (index < input_size) {
            path[index] = '/';
        }
        component_start = index + 1U;
    }

    path[input_size] = '\0';
    return 1;
}

static int open_root_copy(const struct squid_fs_context *context)
{
    if ((context == NULL) || (context->root_fd < 0)) {
        errno = EBADF;
        return -1;
    }
    return dup(context->root_fd);
}

static int open_directory_path(
    const struct squid_fs_context *context,
    const char *path
)
{
    char working[SQUID_FS_PATH_MAX + 1U];
    char *save_pointer = NULL;
    char *component = NULL;
    int directory_fd = open_root_copy(context);

    if (directory_fd < 0) {
        return -1;
    }
    if (path[0] == '\0') {
        return directory_fd;
    }

    memcpy(working, path, strlen(path) + 1U);
    component = strtok_r(working, "/", &save_pointer);
    while (component != NULL) {
        int next_fd = openat(
            directory_fd,
            component,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        );
        int saved_errno = errno;

        close(directory_fd);
        if (next_fd < 0) {
            errno = saved_errno;
            return -1;
        }
        directory_fd = next_fd;
        component = strtok_r(NULL, "/", &save_pointer);
    }

    return directory_fd;
}

static int open_parent_directory(
    const struct squid_fs_context *context,
    const char *path,
    int *parent_fd,
    const char **leaf
)
{
    char parent[SQUID_FS_PATH_MAX + 1U];
    const char *separator = strrchr(path, '/');
    size_t parent_size = 0U;

    if (path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (separator == NULL) {
        parent[0] = '\0';
        *leaf = path;
    } else {
        parent_size = (size_t)(separator - path);
        memcpy(parent, path, parent_size);
        parent[parent_size] = '\0';
        *leaf = separator + 1;
    }

    *parent_fd = open_directory_path(context, parent);
    return *parent_fd >= 0 ? 0 : -1;
}

static uint8_t file_type_from_mode(mode_t mode)
{
    if (S_ISREG(mode)) {
        return SQUID_FS_TYPE_FILE;
    }
    if (S_ISDIR(mode)) {
        return SQUID_FS_TYPE_DIRECTORY;
    }
    return SQUID_FS_TYPE_OTHER;
}

static uint32_t file_size_from_stat(const struct stat *information)
{
    if (!S_ISREG(information->st_mode) || (information->st_size < 0) ||
        ((uint64_t)information->st_size > UINT32_MAX)) {
        return S_ISREG(information->st_mode)
            ? SQUID_FS_SIZE_UNKNOWN
            : 0U;
    }
    return (uint32_t)information->st_size;
}

int init_squid_fs_context(
    struct squid_fs_context *context,
    const char *root_path,
    int read_only
)
{
    if ((context == NULL) || (root_path == NULL) || (root_path[0] == '\0')) {
        return -1;
    }

    context->root_fd = open(
        root_path,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
    );
    if (context->root_fd < 0) {
        return -1;
    }
    context->read_only = read_only != 0;
    return 0;
}

void free_squid_fs_context(struct squid_fs_context *context)
{
    if (context == NULL) {
        return;
    }
    if (context->root_fd >= 0) {
        close(context->root_fd);
    }
    context->root_fd = -1;
    context->read_only = 0;
}

static int handle_capabilities(
    const struct squid_fs_context *context,
    uint8_t opcode,
    uint8_t *response,
    size_t response_capacity
)
{
    uint16_t features = SQUID_FS_FEATURE_STAT |
        SQUID_FS_FEATURE_LIST | SQUID_FS_FEATURE_READ;

    if (response_capacity < 9U) {
        return -1;
    }
    if (!context->read_only) {
        features |= SQUID_FS_FEATURE_WRITE | SQUID_FS_FEATURE_MKDIR |
            SQUID_FS_FEATURE_DELETE | SQUID_FS_FEATURE_RENAME;
    }

    response[0] = (uint8_t)(opcode | SQUID_FS_RESPONSE_BIT);
    response[1] = SQUID_FS_STATUS_OK;
    response[2] = SQUID_FS_PROTOCOL_VERSION;
    write_u16(response + 3U, features);
    response[5] = SQUID_FS_PATH_MAX;
    response[6] = squid_fs_page_header_size;
    response[7] = squid_fs_read_header_size;
    response[8] = response_capacity > squid_fs_read_header_size
        ? (uint8_t)(response_capacity - squid_fs_read_header_size)
        : 0U;
    return 9;
}

static int read_path_request(
    const uint8_t *request,
    size_t request_size,
    size_t length_offset,
    int allow_empty,
    char *path
)
{
    uint8_t path_size = 0U;

    if (request_size <= length_offset) {
        return 0;
    }
    path_size = request[length_offset];
    if ((size_t)path_size != request_size - length_offset - 1U) {
        return 0;
    }
    return path_is_valid(
        request + length_offset + 1U,
        path_size,
        allow_empty,
        path
    );
}

static int handle_stat(
    struct squid_fs_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char path[SQUID_FS_PATH_MAX + 1U];
    struct stat information;
    int parent_fd = -1;
    const char *leaf = NULL;
    int result = 0;

    if (!read_path_request(request, request_size, 1U, 1, path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    if (path[0] == '\0') {
        result = fstat(context->root_fd, &information);
    } else if (open_parent_directory(context, path, &parent_fd, &leaf) == 0) {
        result = fstatat(parent_fd, leaf, &information, AT_SYMLINK_NOFOLLOW);
    } else {
        result = -1;
    }

    if (result != 0) {
        uint8_t status = status_from_errno(errno);
        if (parent_fd >= 0) {
            close(parent_fd);
        }
        return write_response(opcode, status, response, response_capacity);
    }
    if (parent_fd >= 0) {
        close(parent_fd);
    }
    if (response_capacity < 13U) {
        return -1;
    }

    response[0] = (uint8_t)(opcode | SQUID_FS_RESPONSE_BIT);
    response[1] = SQUID_FS_STATUS_OK;
    response[2] = file_type_from_mode(information.st_mode);
    write_u16(response + 3U, (uint16_t)(information.st_mode & 0777U));
    write_u32(response + 5U, file_size_from_stat(&information));
    write_u32(
        response + 9U,
        (information.st_mtime < 0) ||
            ((uint64_t)information.st_mtime > UINT32_MAX)
        ? 0U
        : (uint32_t)information.st_mtime
    );
    return 13;
}

static int handle_list(
    struct squid_fs_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char path[SQUID_FS_PATH_MAX + 1U];
    uint16_t cursor = 0U;
    uint16_t entry_index = 0U;
    uint16_t next_cursor = SQUID_FS_CURSOR_END;
    uint8_t entry_count = 0U;
    size_t output_size = squid_fs_page_header_size;
    int directory_fd = -1;
    DIR *directory = NULL;
    struct dirent *entry = NULL;
    int stopped_for_space = 0;

    if ((request_size < 4U) ||
        !read_path_request(request, request_size, 3U, 1, path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    cursor = read_u16(request + 1U);
    if ((cursor == SQUID_FS_CURSOR_END) ||
        (response_capacity < squid_fs_page_header_size)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    directory_fd = open_directory_path(context, path);
    if (directory_fd < 0) {
        return write_response(
            opcode, status_from_errno(errno), response, response_capacity);
    }
    directory = fdopendir(directory_fd);
    if (directory == NULL) {
        uint8_t status = status_from_errno(errno);
        close(directory_fd);
        return write_response(opcode, status, response, response_capacity);
    }

    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        struct stat information;
        size_t name_size = 0U;
        size_t record_size = 0U;
        uint8_t record[249];

        if ((strcmp(entry->d_name, ".") == 0) ||
            (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        if (entry_index < cursor) {
            ++entry_index;
            continue;
        }

        name_size = strlen(entry->d_name);
        record_size = 6U + name_size;
        if ((name_size > UINT8_MAX) || (record_size > sizeof(record))) {
            closedir(directory);
            return write_response(
                opcode, SQUID_FS_STATUS_TOO_LARGE, response, response_capacity);
        }
        if (fstatat(
            dirfd(directory),
            entry->d_name,
            &information,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
            closedir(directory);
            return write_response(
                opcode, status_from_errno(errno), response, response_capacity);
        }

        if (record_size > response_capacity - output_size) {
            if (entry_count == 0U) {
                closedir(directory);
                return write_response(
                    opcode,
                    SQUID_FS_STATUS_TOO_LARGE,
                    response,
                    response_capacity
                );
            }
            next_cursor = entry_index;
            stopped_for_space = 1;
            break;
        }

        record[0] = file_type_from_mode(information.st_mode);
        record[1] = (uint8_t)name_size;
        memcpy(record + 2U, entry->d_name, name_size);
        write_u32(record + 2U + name_size, file_size_from_stat(&information));
        memcpy(response + output_size, record, record_size);
        output_size += record_size;
        ++entry_count;
        ++entry_index;
    }

    if (!stopped_for_space && (entry == NULL) && (errno != 0)) {
        uint8_t status = status_from_errno(errno);
        closedir(directory);
        return write_response(opcode, status, response, response_capacity);
    }
    closedir(directory);

    response[0] = (uint8_t)(opcode | SQUID_FS_RESPONSE_BIT);
    response[1] = SQUID_FS_STATUS_OK;
    write_u16(response + 2U, next_cursor);
    response[4] = entry_count;
    return (int)output_size;
}

static int handle_read(
    struct squid_fs_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char path[SQUID_FS_PATH_MAX + 1U];
    uint32_t offset = 0U;
    uint8_t maximum_bytes = 0U;
    int parent_fd = -1;
    int file_fd = -1;
    const char *leaf = NULL;
    struct stat information;
    size_t data_size = 0U;
    ssize_t bytes_read = 0;

    if ((request_size < 7U) ||
        !read_path_request(request, request_size, 6U, 0, path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    if (response_capacity < squid_fs_read_header_size) {
        return -1;
    }

    offset = read_u32(request + 1U);
    maximum_bytes = request[5];
    if (open_parent_directory(context, path, &parent_fd, &leaf) != 0) {
        return write_response(
            opcode, status_from_errno(errno), response, response_capacity);
    }
    file_fd = openat(parent_fd, leaf, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file_fd < 0) {
        uint8_t status = status_from_errno(errno);
        close(parent_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    close(parent_fd);

    if (fstat(file_fd, &information) != 0) {
        uint8_t status = status_from_errno(errno);
        close(file_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    if (S_ISDIR(information.st_mode)) {
        close(file_fd);
        return write_response(
            opcode, SQUID_FS_STATUS_IS_DIRECTORY, response, response_capacity);
    }
    if (!S_ISREG(information.st_mode)) {
        close(file_fd);
        return write_response(
            opcode, SQUID_FS_STATUS_ACCESS_DENIED, response, response_capacity);
    }
    if ((information.st_size < 0) ||
        ((uint64_t)information.st_size > UINT32_MAX)) {
        close(file_fd);
        return write_response(
            opcode, SQUID_FS_STATUS_TOO_LARGE, response, response_capacity);
    }
    if ((uint64_t)offset > (uint64_t)information.st_size) {
        close(file_fd);
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    data_size = response_capacity - squid_fs_read_header_size;
    if ((maximum_bytes > 0U) && (data_size > maximum_bytes)) {
        data_size = maximum_bytes;
    }
    if (data_size > UINT8_MAX) {
        data_size = UINT8_MAX;
    }
    if (data_size > (size_t)information.st_size - offset) {
        data_size = (size_t)information.st_size - offset;
    }

    bytes_read = pread(
        file_fd,
        response + squid_fs_read_header_size,
        data_size,
        (off_t)offset
    );
    if (bytes_read < 0) {
        uint8_t status = status_from_errno(errno);
        close(file_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    close(file_fd);

    response[0] = (uint8_t)(opcode | SQUID_FS_RESPONSE_BIT);
    response[1] = SQUID_FS_STATUS_OK;
    write_u32(response + 2U, offset);
    write_u32(response + 6U, (uint32_t)information.st_size);
    response[10] = (uint8_t)bytes_read;
    return (int)(squid_fs_read_header_size + (size_t)bytes_read);
}

static int handle_write(
    struct squid_fs_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char path[SQUID_FS_PATH_MAX + 1U];
    uint32_t offset = 0U;
    uint8_t flags = 0U;
    uint8_t path_size = 0U;
    uint8_t data_size = 0U;
    size_t cursor = 7U;
    int open_flags = O_WRONLY | O_CLOEXEC | O_NOFOLLOW;
    int parent_fd = -1;
    int file_fd = -1;
    const char *leaf = NULL;
    struct stat information;
    ssize_t bytes_written = 0;

    if (context->read_only) {
        return write_response(
            opcode, SQUID_FS_STATUS_READ_ONLY, response, response_capacity);
    }
    if (request_size < 8U) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    offset = read_u32(request + 1U);
    flags = request[5];
    path_size = request[6];
    if ((flags & ~(SQUID_FS_WRITE_CREATE | SQUID_FS_WRITE_TRUNCATE)) != 0U ||
        ((size_t)path_size > request_size - cursor) ||
        !path_is_valid(request + cursor, path_size, 0, path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    cursor += path_size;
    if (cursor >= request_size) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    data_size = request[cursor++];
    if ((size_t)data_size != request_size - cursor) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    if ((flags & SQUID_FS_WRITE_CREATE) != 0U) {
        open_flags |= O_CREAT;
    }
    if ((flags & SQUID_FS_WRITE_TRUNCATE) != 0U) {
        open_flags |= O_TRUNC;
    }
    if (open_parent_directory(context, path, &parent_fd, &leaf) != 0) {
        return write_response(
            opcode, status_from_errno(errno), response, response_capacity);
    }
    file_fd = openat(parent_fd, leaf, open_flags, 0666);
    if (file_fd < 0) {
        uint8_t status = status_from_errno(errno);
        close(parent_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    close(parent_fd);

    if (fstat(file_fd, &information) != 0) {
        uint8_t status = status_from_errno(errno);
        close(file_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    if (!S_ISREG(information.st_mode)) {
        close(file_fd);
        return write_response(
            opcode, SQUID_FS_STATUS_ACCESS_DENIED, response, response_capacity);
    }

    bytes_written = pwrite(file_fd, request + cursor, data_size, (off_t)offset);
    if (bytes_written < 0) {
        uint8_t status = status_from_errno(errno);
        close(file_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    close(file_fd);

    if (response_capacity < 7U) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_FS_RESPONSE_BIT);
    response[1] = SQUID_FS_STATUS_OK;
    write_u32(response + 2U, offset);
    response[6] = (uint8_t)bytes_written;
    return 7;
}

static int handle_mkdir_or_delete(
    struct squid_fs_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char path[SQUID_FS_PATH_MAX + 1U];
    int parent_fd = -1;
    const char *leaf = NULL;
    int result = -1;

    if (context->read_only) {
        return write_response(
            opcode, SQUID_FS_STATUS_READ_ONLY, response, response_capacity);
    }
    if (!read_path_request(request, request_size, 1U, 0, path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    if (open_parent_directory(context, path, &parent_fd, &leaf) != 0) {
        return write_response(
            opcode, status_from_errno(errno), response, response_capacity);
    }

    if (opcode == SQUID_FS_OP_MKDIR) {
        result = mkdirat(parent_fd, leaf, 0777);
    } else {
        struct stat information;
        result = fstatat(parent_fd, leaf, &information, AT_SYMLINK_NOFOLLOW);
        if (result == 0) {
            result = unlinkat(
                parent_fd,
                leaf,
                S_ISDIR(information.st_mode) ? AT_REMOVEDIR : 0
            );
        }
    }

    if (result != 0) {
        uint8_t status = status_from_errno(errno);
        close(parent_fd);
        return write_response(opcode, status, response, response_capacity);
    }
    close(parent_fd);
    return write_response(opcode, SQUID_FS_STATUS_OK, response, response_capacity);
}

static int handle_rename(
    struct squid_fs_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char old_path[SQUID_FS_PATH_MAX + 1U];
    char new_path[SQUID_FS_PATH_MAX + 1U];
    uint8_t old_size = 0U;
    uint8_t new_size = 0U;
    size_t cursor = 2U;
    int old_parent = -1;
    int new_parent = -1;
    const char *old_leaf = NULL;
    const char *new_leaf = NULL;
    int result = -1;

    if (context->read_only) {
        return write_response(
            opcode, SQUID_FS_STATUS_READ_ONLY, response, response_capacity);
    }
    if (request_size < 4U) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    old_size = request[1];
    if (((size_t)old_size > request_size - cursor) ||
        !path_is_valid(request + cursor, old_size, 0, old_path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    cursor += old_size;
    if (cursor >= request_size) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
    new_size = request[cursor++];
    if (((size_t)new_size != request_size - cursor) ||
        !path_is_valid(request + cursor, new_size, 0, new_path)) {
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }

    if (open_parent_directory(context, old_path, &old_parent, &old_leaf) != 0 ||
        open_parent_directory(context, new_path, &new_parent, &new_leaf) != 0) {
        uint8_t status = status_from_errno(errno);
        if (old_parent >= 0) {
            close(old_parent);
        }
        if (new_parent >= 0) {
            close(new_parent);
        }
        return write_response(opcode, status, response, response_capacity);
    }

    result = renameat(old_parent, old_leaf, new_parent, new_leaf);
    if (result != 0) {
        uint8_t status = status_from_errno(errno);
        close(old_parent);
        close(new_parent);
        return write_response(opcode, status, response, response_capacity);
    }
    close(old_parent);
    close(new_parent);
    return write_response(opcode, SQUID_FS_STATUS_OK, response, response_capacity);
}

int handle_squid_fs_request(
    struct squid_fs_context *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint8_t opcode = 0U;

    if ((context == NULL) || (context->root_fd < 0) || (request == NULL) ||
        (request_size == 0U) || (response == NULL) ||
        (response_capacity < 2U)) {
        return -1;
    }

    opcode = request[0];
    switch (opcode) {
    case SQUID_FS_OP_CAPABILITIES:
        if (request_size != 1U) {
            return write_response(
                opcode,
                SQUID_FS_STATUS_BAD_REQUEST,
                response,
                response_capacity
            );
        }
        return handle_capabilities(context, opcode, response, response_capacity);
    case SQUID_FS_OP_STAT:
        return handle_stat(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_FS_OP_LIST:
        return handle_list(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_FS_OP_READ:
        return handle_read(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_FS_OP_WRITE:
        return handle_write(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_FS_OP_MKDIR:
    case SQUID_FS_OP_DELETE:
        return handle_mkdir_or_delete(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_FS_OP_RENAME:
        return handle_rename(
            context, opcode, request, request_size, response, response_capacity);
    default:
        return write_response(
            opcode, SQUID_FS_STATUS_BAD_REQUEST, response, response_capacity);
    }
}
