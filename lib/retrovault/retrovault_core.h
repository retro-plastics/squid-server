#ifndef RETROVAULT_CORE_H
#define RETROVAULT_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct retro_vault_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t limit;
    int overflowed;
};

#define retro_vault_http_ok         0
#define retro_vault_http_error     -1
#define retro_vault_http_too_large -2

typedef int (*retro_vault_http_get_fn)(
    void *user_data,
    const char *path,
    size_t size_limit,
    struct retro_vault_buffer *response,
    long *status_code
);

struct retro_vault_http_client {
    retro_vault_http_get_fn get;
    void *user_data;
};

struct retro_vault_download {
    char *id;
    char *label;
    char *format;
    uint32_t aggregate_size;
    uint8_t file_count;
};

struct retro_vault_package {
    char *id;
    char *name;
    char *vendor;
    char *platform_id;
    char *platform_name;
    char *model_id;
    char *model_name;
    char *version;
    char *description;
    uint16_t release_year;
    uint8_t rating;
    struct retro_vault_download *downloads;
    size_t download_count;
};

struct retro_vault_context {
    struct retro_vault_http_client http;
    struct retro_vault_package *packages;
    size_t package_count;
    time_t catalog_fetched_at;
    unsigned int catalog_cache_seconds;
    struct retro_vault_buffer download_data;
    char *download_path;
};

void retro_vault_buffer_init(struct retro_vault_buffer *buffer);
void retro_vault_buffer_free(struct retro_vault_buffer *buffer);
int retro_vault_buffer_append(
    struct retro_vault_buffer *buffer,
    const void *data,
    size_t size
);

void init_retro_vault_context(
    struct retro_vault_context *context,
    const struct retro_vault_http_client *http
);

void free_retro_vault_context(struct retro_vault_context *context);

int handle_retro_vault_request(
    struct retro_vault_context *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
);

#endif
