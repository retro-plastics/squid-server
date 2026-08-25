/* Rooted filesystem plugin client. */
#ifndef SQUID_CLIENT_FILESYSTEM_H
#define SQUID_CLIENT_FILESYSTEM_H

#include "squid_client/base.h"
#include "squid_server/filesystem_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct squid_client_fs_stat {
    uint8_t type;
    uint16_t mode;
    uint32_t size;
    uint32_t modification_time;
} squid_client_fs_stat_t;

typedef struct squid_client_fs_list {
    uint16_t next_cursor;
    uint8_t entries_left;
    const uint8_t *next;
    const uint8_t *end;
} squid_client_fs_list_t;

typedef struct squid_client_fs_entry {
    uint8_t type;
    squid_client_bytes_t name;
    uint32_t size;
} squid_client_fs_entry_t;

typedef struct squid_client_fs_read {
    uint32_t offset;
    uint32_t total_size;
    squid_client_bytes_t data;
} squid_client_fs_read_t;

int squid_client_fs_stat(
    squid_client_t *client,
    const char *path,
    squid_client_fs_stat_t *value
);
int squid_client_fs_list(
    squid_client_t *client,
    const char *path,
    uint16_t cursor,
    squid_client_fs_list_t *page
);
/* Returns 1 for an entry, 0 at end, or a negative client error. */
int squid_client_fs_list_next(
    squid_client_fs_list_t *page,
    squid_client_fs_entry_t *entry
);
int squid_client_fs_read(
    squid_client_t *client,
    const char *path,
    uint32_t offset,
    uint8_t maximum_bytes,
    squid_client_fs_read_t *chunk
);
int squid_client_fs_write(
    squid_client_t *client,
    const char *path,
    uint32_t offset,
    uint8_t flags,
    const void *data,
    uint8_t size,
    uint8_t *written
);
int squid_client_fs_mkdir(squid_client_t *client, const char *path);
int squid_client_fs_delete(squid_client_t *client, const char *path);
int squid_client_fs_rename(
    squid_client_t *client,
    const char *old_path,
    const char *new_path
);

#ifdef __cplusplus
}
#endif

#endif
