#ifndef SQUID_FILESYSTEM_CORE_H
#define SQUID_FILESYSTEM_CORE_H

#include <stddef.h>
#include <stdint.h>

struct squid_fs_context {
    int root_fd;
    int read_only;
};

int init_squid_fs_context(
    struct squid_fs_context *context,
    const char *root_path,
    int read_only
);

void free_squid_fs_context(struct squid_fs_context *context);

int handle_squid_fs_request(
    struct squid_fs_context *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
);

#endif
