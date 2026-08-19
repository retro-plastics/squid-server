#include "retrovault_curl.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define retro_vault_connect_timeout_seconds 10L
#define retro_vault_request_timeout_seconds 60L

static size_t receive_http_data(
    char *data,
    size_t element_size,
    size_t element_count,
    void *user_data
)
{
    struct retro_vault_buffer *buffer = user_data;
    size_t data_size = 0U;

    if ((element_size != 0U) && (element_count > SIZE_MAX / element_size)) {
        return 0U;
    }
    data_size = element_size * element_count;

    return retro_vault_buffer_append(buffer, data, data_size) == 0
        ? data_size
        : 0U;
}

int init_retro_vault_curl_client(struct retro_vault_curl_client *client)
{
    const char *configured_url = NULL;
    size_t url_size = 0U;

    if (client == NULL) {
        return -1;
    }

    memset(client, 0, sizeof(*client));
    configured_url = getenv("RETRO_VAULT_API_URL");
    if ((configured_url == NULL) || (configured_url[0] == '\0')) {
        configured_url = "https://retro-vault.org";
    }

    if ((strncmp(configured_url, "https://", 8U) != 0) &&
        (strncmp(configured_url, "http://", 7U) != 0)) {
        return -1;
    }

    url_size = strlen(configured_url);
    while ((url_size > 0U) && (configured_url[url_size - 1U] == '/')) {
        --url_size;
    }
    if ((url_size == 0U) || (url_size >= sizeof(client->base_url))) {
        return -1;
    }

    memcpy(client->base_url, configured_url, url_size);
    client->base_url[url_size] = '\0';

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        memset(client, 0, sizeof(*client));
        return -1;
    }

    client->initialized = 1;
    return 0;
}

void free_retro_vault_curl_client(struct retro_vault_curl_client *client)
{
    if ((client != NULL) && client->initialized) {
        curl_global_cleanup();
        memset(client, 0, sizeof(*client));
    }
}

int get_retro_vault_curl(
    void *user_data,
    const char *path,
    size_t size_limit,
    struct retro_vault_buffer *response,
    long *status_code
)
{
    struct retro_vault_curl_client *client = user_data;
    CURL *curl = NULL;
    CURLcode result = CURLE_OK;
    char url[1024];

    if ((client == NULL) || !client->initialized || (path == NULL) ||
        (path[0] != '/') || (response == NULL) || (status_code == NULL) ||
        (size_limit == 0U)) {
        return retro_vault_http_error;
    }

    if (snprintf(url, sizeof(url), "%s%s", client->base_url, path) >=
        (int)sizeof(url)) {
        return retro_vault_http_error;
    }

    retro_vault_buffer_free(response);
    response->limit = size_limit;
    *status_code = 0L;

    curl = curl_easy_init();
    if (curl == NULL) {
        return retro_vault_http_error;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_http_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, retro_vault_connect_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, retro_vault_request_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "squid-server-retrovault/1");

    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        return response->overflowed
            ? retro_vault_http_too_large
            : retro_vault_http_error;
    }
    return retro_vault_http_ok;
}
