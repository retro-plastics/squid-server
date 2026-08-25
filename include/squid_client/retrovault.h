/* Retro Vault plugin client. */
#ifndef SQUID_CLIENT_RETROVAULT_H
#define SQUID_CLIENT_RETROVAULT_H

#include "squid_client/base.h"
#include "squid_server/retrovault_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct squid_client_retro_list {
    uint16_t next_cursor;
    uint8_t entries_left;
    const uint8_t *next;
    const uint8_t *end;
} squid_client_retro_list_t;

typedef struct squid_client_retro_entry {
    squid_client_bytes_t id;
    squid_client_bytes_t name;
} squid_client_retro_entry_t;

typedef struct squid_client_retro_info {
    uint16_t next_cursor;
    uint8_t values_left;
    const uint8_t *next;
    const uint8_t *end;
} squid_client_retro_info_t;

typedef struct squid_client_retro_value {
    uint8_t type;
    squid_client_bytes_t value;
} squid_client_retro_value_t;

typedef struct squid_client_retro_download {
    uint32_t offset;
    uint32_t total_size;
    squid_client_bytes_t data;
} squid_client_retro_download_t;

int squid_client_retro_list(
    squid_client_t *client,
    const char *platform,
    const char *model,
    uint16_t cursor,
    squid_client_retro_list_t *page
);
int squid_client_retro_search(
    squid_client_t *client,
    const char *platform,
    const char *model,
    const char *query,
    uint16_t cursor,
    squid_client_retro_list_t *page
);
/* Returns 1 for an item, 0 at end, or a negative client error. */
int squid_client_retro_list_next(
    squid_client_retro_list_t *page,
    squid_client_retro_entry_t *entry
);
int squid_client_retro_info(
    squid_client_t *client,
    const char *package_id,
    uint16_t cursor,
    squid_client_retro_info_t *page
);
/* Returns 1 for a value, 0 at end, or a negative client error. */
int squid_client_retro_info_next(
    squid_client_retro_info_t *page,
    squid_client_retro_value_t *value
);
int squid_client_retro_download(
    squid_client_t *client,
    const char *package_id,
    const char *download_id,
    uint32_t offset,
    uint8_t maximum_bytes,
    squid_client_retro_download_t *chunk
);

#ifdef __cplusplus
}
#endif

#endif
