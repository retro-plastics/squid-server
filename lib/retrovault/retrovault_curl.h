#ifndef RETROVAULT_CURL_H
#define RETROVAULT_CURL_H

#include "retrovault_core.h"

#define retro_vault_base_url_max 256U

struct retro_vault_curl_client {
    char base_url[retro_vault_base_url_max];
    int initialized;
};

int init_retro_vault_curl_client(struct retro_vault_curl_client *client);
void free_retro_vault_curl_client(struct retro_vault_curl_client *client);

int get_retro_vault_curl(
    void *user_data,
    const char *path,
    size_t size_limit,
    struct retro_vault_buffer *response,
    long *status_code
);

#endif
