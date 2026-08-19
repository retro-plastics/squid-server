#include "retrovault_core.h"

#include "retrovault_json.h"
#include "squid_server/retrovault_protocol.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define retro_vault_catalog_limit  (4U * 1024U * 1024U)
#define retro_vault_download_limit (64U * 1024U * 1024U)
#define retro_vault_page_header_size 5U
#define retro_vault_download_header_size 11U

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)((value >> 8U) & 0xFFU);
    output[2] = (uint8_t)((value >> 16U) & 0xFFU);
    output[3] = (uint8_t)((value >> 24U) & 0xFFU);
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

static char *duplicate_text(const char *text)
{
    char *copy = NULL;
    size_t length = 0U;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

void retro_vault_buffer_init(struct retro_vault_buffer *buffer)
{
    if (buffer != NULL) {
        memset(buffer, 0, sizeof(*buffer));
    }
}

void retro_vault_buffer_free(struct retro_vault_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

int retro_vault_buffer_append(
    struct retro_vault_buffer *buffer,
    const void *data,
    size_t size
)
{
    size_t required = 0U;
    size_t capacity = 0U;
    uint8_t *resized = NULL;

    if ((buffer == NULL) || ((data == NULL) && (size > 0U))) {
        return -1;
    }

    if ((size > buffer->limit) || (buffer->size > buffer->limit - size)) {
        buffer->overflowed = 1;
        return -1;
    }

    required = buffer->size + size;
    if (required > buffer->capacity) {
        capacity = buffer->capacity > 0U ? buffer->capacity : 4096U;
        while (capacity < required) {
            if (capacity > buffer->limit / 2U) {
                capacity = buffer->limit;
                break;
            }
            capacity *= 2U;
        }

        resized = realloc(buffer->data, capacity);
        if (resized == NULL) {
            return -1;
        }
        buffer->data = resized;
        buffer->capacity = capacity;
    }

    if (size > 0U) {
        memcpy(buffer->data + buffer->size, data, size);
        buffer->size += size;
    }
    return 0;
}

static void free_download(struct retro_vault_download *download)
{
    if (download == NULL) {
        return;
    }

    free(download->id);
    free(download->label);
    free(download->format);
    memset(download, 0, sizeof(*download));
}

static void free_package(struct retro_vault_package *package)
{
    size_t index = 0U;

    if (package == NULL) {
        return;
    }

    free(package->id);
    free(package->name);
    free(package->vendor);
    free(package->platform_id);
    free(package->platform_name);
    free(package->version);
    free(package->description);

    for (index = 0U; index < package->download_count; ++index) {
        free_download(&package->downloads[index]);
    }
    free(package->downloads);
    memset(package, 0, sizeof(*package));
}

static void free_packages(
    struct retro_vault_package *packages,
    size_t package_count
)
{
    size_t index = 0U;

    for (index = 0U; index < package_count; ++index) {
        free_package(&packages[index]);
    }
    free(packages);
}

void init_retro_vault_context(
    struct retro_vault_context *context,
    const struct retro_vault_http_client *http
)
{
    if (context == NULL) {
        return;
    }

    memset(context, 0, sizeof(*context));
    if (http != NULL) {
        context->http = *http;
    }
    context->catalog_cache_seconds = 3600U;
    retro_vault_buffer_init(&context->download_data);
}

void free_retro_vault_context(struct retro_vault_context *context)
{
    if (context == NULL) {
        return;
    }

    free_packages(context->packages, context->package_count);
    retro_vault_buffer_free(&context->download_data);
    free(context->download_package_id);
    free(context->download_id);
    memset(context, 0, sizeof(*context));
}

static int get_json_string_member(
    const struct retro_vault_json_value *object,
    const char *name,
    int required,
    char **text
)
{
    struct retro_vault_json_value value;
    int found = get_retro_vault_json_member(object, name, &value);

    if (found < 0) {
        return -1;
    }
    if ((found == 0) || (value.type == retro_vault_json_null)) {
        *text = duplicate_text("");
        return (required && ((*text == NULL) || ((*text)[0] == '\0'))) ? -1 : 0;
    }
    if (value.type != retro_vault_json_string) {
        return -1;
    }

    *text = duplicate_retro_vault_json_string(&value);
    if ((*text == NULL) || (required && ((*text)[0] == '\0'))) {
        free(*text);
        *text = NULL;
        return -1;
    }
    return 0;
}

static int get_json_uint64_member(
    const struct retro_vault_json_value *object,
    const char *name,
    uint64_t *number,
    int *known
)
{
    struct retro_vault_json_value value;
    int found = get_retro_vault_json_member(object, name, &value);

    *known = 0;
    if (found < 0) {
        return -1;
    }
    if ((found == 0) || (value.type == retro_vault_json_null)) {
        return 0;
    }
    if (get_retro_vault_json_uint64(&value, number) != 0) {
        return -1;
    }

    *known = 1;
    return 0;
}

static uint8_t parse_rating(const char *rating)
{
    if (rating == NULL) {
        return RETRO_VAULT_RATING_UNKNOWN;
    }
    if (strcmp(rating, "Curious") == 0) {
        return RETRO_VAULT_RATING_CURIOUS;
    }
    if (strcmp(rating, "Average") == 0) {
        return RETRO_VAULT_RATING_AVERAGE;
    }
    if (strcmp(rating, "Great") == 0) {
        return RETRO_VAULT_RATING_GREAT;
    }
    if (strcmp(rating, "Legendary") == 0) {
        return RETRO_VAULT_RATING_LEGENDARY;
    }
    return RETRO_VAULT_RATING_UNKNOWN;
}

static int parse_download_files(
    const struct retro_vault_json_value *download_object,
    struct retro_vault_download *download
)
{
    struct retro_vault_json_value files;
    struct retro_vault_json_iterator iterator;
    struct retro_vault_json_value file;
    uint64_t total = 0U;
    int all_sizes_known = 1;
    int result = 0;

    result = get_retro_vault_json_member(download_object, "files", &files);
    if ((result <= 0) || (files.type != retro_vault_json_array) ||
        (init_retro_vault_json_array_iterator(&files, &iterator) != 0)) {
        return -1;
    }

    while ((result = next_retro_vault_json_array_value(&iterator, &file)) > 0) {
        uint64_t size = 0U;
        int known = 0;

        if ((file.type != retro_vault_json_object) ||
            (get_json_uint64_member(&file, "sizeBytes", &size, &known) != 0)) {
            return -1;
        }

        if (download->file_count < UINT8_MAX) {
            ++download->file_count;
        }

        if (!known || (total > UINT32_MAX - size)) {
            all_sizes_known = 0;
        } else {
            total += size;
        }
    }

    if (result < 0) {
        return -1;
    }

    download->aggregate_size = all_sizes_known
        ? (uint32_t)total
        : RETRO_VAULT_SIZE_UNKNOWN;
    return 0;
}

static int parse_download(
    const struct retro_vault_json_value *object,
    struct retro_vault_download *download
)
{
    memset(download, 0, sizeof(*download));

    if ((object->type != retro_vault_json_object) ||
        (get_json_string_member(object, "id", 1, &download->id) != 0) ||
        (get_json_string_member(object, "label", 1, &download->label) != 0) ||
        (get_json_string_member(object, "format", 1, &download->format) != 0) ||
        (parse_download_files(object, download) != 0)) {
        free_download(download);
        return -1;
    }

    return 0;
}

static int parse_downloads(
    const struct retro_vault_json_value *object,
    struct retro_vault_package *package
)
{
    struct retro_vault_json_value downloads;
    struct retro_vault_json_iterator iterator;
    struct retro_vault_json_value item;
    int found = get_retro_vault_json_member(object, "downloads", &downloads);
    int result = 0;

    if (found < 0) {
        return -1;
    }
    if ((found == 0) || (downloads.type == retro_vault_json_null)) {
        return 0;
    }
    if ((downloads.type != retro_vault_json_array) ||
        (init_retro_vault_json_array_iterator(&downloads, &iterator) != 0)) {
        return -1;
    }

    while ((result = next_retro_vault_json_array_value(&iterator, &item)) > 0) {
        struct retro_vault_download *resized = NULL;

        resized = realloc(
            package->downloads,
            (package->download_count + 1U) * sizeof(*package->downloads)
        );
        if (resized == NULL) {
            return -1;
        }
        package->downloads = resized;

        if (parse_download(
            &item,
            &package->downloads[package->download_count]
        ) != 0) {
            return -1;
        }
        ++package->download_count;
    }

    return result < 0 ? -1 : 0;
}

static int parse_package(
    const struct retro_vault_json_value *object,
    struct retro_vault_package *package
)
{
    char *rating = NULL;
    uint64_t year = 0U;
    int year_known = 0;

    memset(package, 0, sizeof(*package));
    if ((object->type != retro_vault_json_object) ||
        (get_json_string_member(object, "id", 1, &package->id) != 0) ||
        (get_json_string_member(object, "name", 1, &package->name) != 0) ||
        (get_json_string_member(object, "vendor", 0, &package->vendor) != 0) ||
        (get_json_string_member(object, "platformId", 0, &package->platform_id) != 0) ||
        (get_json_string_member(object, "platformName", 0, &package->platform_name) != 0) ||
        (get_json_string_member(object, "version", 0, &package->version) != 0) ||
        (get_json_string_member(object, "description", 0, &package->description) != 0) ||
        (get_json_string_member(object, "rating", 0, &rating) != 0) ||
        (get_json_uint64_member(object, "releaseYear", &year, &year_known) != 0) ||
        (parse_downloads(object, package) != 0)) {
        free(rating);
        free_package(package);
        return -1;
    }

    package->rating = parse_rating(rating);
    free(rating);
    package->release_year = year_known && (year <= UINT16_MAX)
        ? (uint16_t)year
        : 0U;
    return 0;
}

static int parse_catalog(
    const uint8_t *data,
    size_t size,
    struct retro_vault_package **packages,
    size_t *package_count
)
{
    struct retro_vault_json_value root;
    struct retro_vault_json_iterator iterator;
    struct retro_vault_json_value item;
    struct retro_vault_package *parsed = NULL;
    size_t count = 0U;
    int result = 0;

    if ((parse_retro_vault_json((const char *)data, size, &root) != 0) ||
        (root.type != retro_vault_json_array) ||
        (init_retro_vault_json_array_iterator(&root, &iterator) != 0)) {
        return -1;
    }

    while ((result = next_retro_vault_json_array_value(&iterator, &item)) > 0) {
        struct retro_vault_package *resized = NULL;

        if (count >= (size_t)RETRO_VAULT_CURSOR_END) {
            free_packages(parsed, count);
            return -1;
        }

        resized = realloc(parsed, (count + 1U) * sizeof(*parsed));
        if (resized == NULL) {
            free_packages(parsed, count);
            return -1;
        }
        parsed = resized;

        if (parse_package(&item, &parsed[count]) != 0) {
            free_packages(parsed, count);
            return -1;
        }
        ++count;
    }

    if (result < 0) {
        free_packages(parsed, count);
        return -1;
    }

    *packages = parsed;
    *package_count = count;
    return 0;
}

static uint8_t http_error_status(int http_result, long status_code)
{
    if (http_result == retro_vault_http_too_large) {
        return RETRO_VAULT_STATUS_TOO_LARGE;
    }
    if (status_code == 404L) {
        return RETRO_VAULT_STATUS_NOT_FOUND;
    }
    return RETRO_VAULT_STATUS_API_UNAVAILABLE;
}

static uint8_t ensure_catalog(struct retro_vault_context *context)
{
    struct retro_vault_buffer response;
    struct retro_vault_package *packages = NULL;
    size_t package_count = 0U;
    time_t now = time(NULL);
    long status_code = 0L;
    int result = 0;

    if ((context->package_count > 0U) &&
        (now != (time_t)-1) &&
        (context->catalog_fetched_at != (time_t)0) &&
        ((unsigned long)(now - context->catalog_fetched_at) <
         context->catalog_cache_seconds)) {
        return RETRO_VAULT_STATUS_OK;
    }

    if (context->http.get == NULL) {
        return RETRO_VAULT_STATUS_API_UNAVAILABLE;
    }

    retro_vault_buffer_init(&response);
    result = context->http.get(
        context->http.user_data,
        "/api/v1/catalog/packages",
        retro_vault_catalog_limit,
        &response,
        &status_code
    );

    if ((result != retro_vault_http_ok) ||
        (status_code < 200L) || (status_code >= 300L) ||
        (parse_catalog(response.data, response.size, &packages, &package_count) != 0)) {
        retro_vault_buffer_free(&response);
        if (context->package_count > 0U) {
            return RETRO_VAULT_STATUS_OK;
        }
        return result == retro_vault_http_ok
            ? (status_code == 404L
                ? RETRO_VAULT_STATUS_NOT_FOUND
                : RETRO_VAULT_STATUS_INTERNAL_ERROR)
            : http_error_status(result, status_code);
    }

    retro_vault_buffer_free(&response);
    free_packages(context->packages, context->package_count);
    context->packages = packages;
    context->package_count = package_count;
    context->catalog_fetched_at = now != (time_t)-1 ? now : (time_t)1;
    return RETRO_VAULT_STATUS_OK;
}

static int text_equals_case_insensitive(
    const char *left,
    const uint8_t *right,
    size_t right_size
)
{
    size_t index = 0U;

    if ((left == NULL) || (strlen(left) != right_size)) {
        return 0;
    }
    for (index = 0U; index < right_size; ++index) {
        if (tolower((unsigned char)left[index]) !=
            tolower((unsigned char)right[index])) {
            return 0;
        }
    }
    return 1;
}

static int text_contains_case_insensitive(
    const char *text,
    const uint8_t *query,
    size_t query_size
)
{
    size_t text_size = 0U;
    size_t start = 0U;
    size_t index = 0U;

    if ((text == NULL) || (query == NULL) || (query_size == 0U)) {
        return 0;
    }

    text_size = strlen(text);
    if (query_size > text_size) {
        return 0;
    }

    for (start = 0U; start <= text_size - query_size; ++start) {
        for (index = 0U; index < query_size; ++index) {
            if (tolower((unsigned char)text[start + index]) !=
                tolower((unsigned char)query[index])) {
                break;
            }
        }
        if (index == query_size) {
            return 1;
        }
    }
    return 0;
}

static int package_matches(
    const struct retro_vault_package *package,
    const uint8_t *platform,
    size_t platform_size,
    const uint8_t *query,
    size_t query_size
)
{
    if ((platform_size > 0U) &&
        !text_equals_case_insensitive(package->platform_id, platform, platform_size)) {
        return 0;
    }

    if (query_size == 0U) {
        return 1;
    }

    return text_contains_case_insensitive(package->id, query, query_size) ||
        text_contains_case_insensitive(package->name, query, query_size) ||
        text_contains_case_insensitive(package->vendor, query, query_size) ||
        text_contains_case_insensitive(package->description, query, query_size);
}

static size_t make_list_entry(
    const struct retro_vault_package *package,
    uint8_t *entry,
    size_t entry_capacity
)
{
    size_t id_size = strlen(package->id);
    size_t name_size = strlen(package->name);
    size_t maximum_name = 0U;

    if ((entry_capacity < 2U) || (id_size > UINT8_MAX)) {
        return 0U;
    }
    if (id_size > entry_capacity - 2U) {
        return 0U;
    }

    maximum_name = entry_capacity - id_size - 2U;
    if (name_size > maximum_name) {
        name_size = maximum_name;
        while ((name_size > 0U) &&
               (((unsigned char)package->name[name_size] & 0xC0U) == 0x80U)) {
            --name_size;
        }
    }
    if (name_size > UINT8_MAX) {
        name_size = UINT8_MAX;
    }

    entry[0] = (uint8_t)id_size;
    memcpy(entry + 1U, package->id, id_size);
    entry[1U + id_size] = (uint8_t)name_size;
    memcpy(entry + 2U + id_size, package->name, name_size);
    return 2U + id_size + name_size;
}

static int write_error_response(
    uint8_t opcode,
    uint8_t status,
    uint8_t *response,
    size_t response_capacity
)
{
    if ((response == NULL) || (response_capacity < 2U)) {
        return -1;
    }

    response[0] = (uint8_t)(opcode | RETRO_VAULT_RESPONSE_BIT);
    response[1] = status;
    return 2;
}

static int write_catalog_page(
    struct retro_vault_context *context,
    uint8_t opcode,
    uint16_t cursor,
    const uint8_t *platform,
    size_t platform_size,
    const uint8_t *query,
    size_t query_size,
    uint8_t *response,
    size_t response_capacity
)
{
    size_t package_index = 0U;
    uint16_t match_index = 0U;
    uint16_t next_cursor = RETRO_VAULT_CURSOR_END;
    uint8_t entry_count = 0U;
    size_t output_size = retro_vault_page_header_size;

    if (response_capacity < retro_vault_page_header_size) {
        return -1;
    }

    for (package_index = 0U; package_index < context->package_count; ++package_index) {
        uint8_t entry[249];
        size_t entry_size = 0U;

        if (!package_matches(
            &context->packages[package_index],
            platform,
            platform_size,
            query,
            query_size
        )) {
            continue;
        }

        if (match_index < cursor) {
            ++match_index;
            continue;
        }

        entry_size = make_list_entry(
            &context->packages[package_index],
            entry,
            sizeof(entry)
        );
        if (entry_size == 0U) {
            ++match_index;
            continue;
        }

        if (entry_size > response_capacity - output_size) {
            if (entry_count == 0U) {
                entry_size = make_list_entry(
                    &context->packages[package_index],
                    entry,
                    response_capacity - output_size
                );
                if (entry_size == 0U) {
                    return write_error_response(
                        opcode,
                        RETRO_VAULT_STATUS_TOO_LARGE,
                        response,
                        response_capacity
                    );
                }
            } else {
                next_cursor = match_index;
                break;
            }
        }

        memcpy(response + output_size, entry, entry_size);
        output_size += entry_size;
        ++entry_count;
        ++match_index;
    }

    response[0] = (uint8_t)(opcode | RETRO_VAULT_RESPONSE_BIT);
    response[1] = RETRO_VAULT_STATUS_OK;
    write_u16(response + 2U, next_cursor);
    response[4] = entry_count;
    return (int)output_size;
}

static const struct retro_vault_package *find_package(
    const struct retro_vault_context *context,
    const uint8_t *id,
    size_t id_size
)
{
    size_t index = 0U;

    for (index = 0U; index < context->package_count; ++index) {
        if (text_equals_case_insensitive(context->packages[index].id, id, id_size)) {
            return &context->packages[index];
        }
    }
    return NULL;
}

static size_t make_string_tlv(
    uint8_t type,
    const char *text,
    uint8_t *record,
    size_t record_capacity
)
{
    size_t text_size = text != NULL ? strlen(text) : 0U;

    if (record_capacity < 2U) {
        return 0U;
    }
    if (text_size > record_capacity - 2U) {
        text_size = record_capacity - 2U;
        while ((text_size > 0U) &&
               (((unsigned char)text[text_size] & 0xC0U) == 0x80U)) {
            --text_size;
        }
    }
    if (text_size > UINT8_MAX) {
        text_size = UINT8_MAX;
    }

    record[0] = type;
    record[1] = (uint8_t)text_size;
    if (text_size > 0U) {
        memcpy(record + 2U, text, text_size);
    }
    return text_size + 2U;
}

static size_t append_counted_text(
    uint8_t *output,
    size_t output_capacity,
    const char *text,
    size_t maximum_size
)
{
    size_t text_size = text != NULL ? strlen(text) : 0U;

    if (output_capacity < 1U) {
        return 0U;
    }
    if (text_size > maximum_size) {
        text_size = maximum_size;
    }
    if (text_size > output_capacity - 1U) {
        text_size = output_capacity - 1U;
    }
    if (text_size > UINT8_MAX) {
        text_size = UINT8_MAX;
    }
    while ((text_size > 0U) && (text != NULL) &&
           (((unsigned char)text[text_size] & 0xC0U) == 0x80U)) {
        --text_size;
    }

    output[0] = (uint8_t)text_size;
    memcpy(output + 1U, text, text_size);
    return text_size + 1U;
}

static size_t make_download_tlv(
    const struct retro_vault_download *download,
    uint8_t *record,
    size_t record_capacity
)
{
    size_t output_size = 2U;
    size_t written = 0U;

    if (record_capacity < 10U) {
        return 0U;
    }

    record[0] = RETRO_VAULT_INFO_DOWNLOAD;

    written = append_counted_text(
        record + output_size,
        record_capacity - output_size - 5U,
        download->id,
        64U
    );
    if (written == 0U) {
        return 0U;
    }
    output_size += written;

    written = append_counted_text(
        record + output_size,
        record_capacity - output_size - 5U,
        download->label,
        96U
    );
    if (written == 0U) {
        return 0U;
    }
    output_size += written;

    written = append_counted_text(
        record + output_size,
        record_capacity - output_size - 5U,
        download->format,
        32U
    );
    if (written == 0U) {
        return 0U;
    }
    output_size += written;

    write_u32(record + output_size, download->aggregate_size);
    output_size += 4U;
    record[output_size++] = download->file_count;
    record[1] = (uint8_t)(output_size - 2U);
    return output_size;
}

static size_t make_info_record(
    const struct retro_vault_package *package,
    uint16_t cursor,
    uint8_t *record,
    size_t record_capacity
)
{
    switch (cursor) {
    case 0U:
        return make_string_tlv(
            RETRO_VAULT_INFO_ID, package->id, record, record_capacity);
    case 1U:
        return make_string_tlv(
            RETRO_VAULT_INFO_NAME, package->name, record, record_capacity);
    case 2U:
        return make_string_tlv(
            RETRO_VAULT_INFO_VENDOR, package->vendor, record, record_capacity);
    case 3U:
        return make_string_tlv(
            RETRO_VAULT_INFO_PLATFORM_ID,
            package->platform_id,
            record,
            record_capacity
        );
    case 4U:
        return make_string_tlv(
            RETRO_VAULT_INFO_PLATFORM_NAME,
            package->platform_name,
            record,
            record_capacity
        );
    case 5U:
        return make_string_tlv(
            RETRO_VAULT_INFO_VERSION, package->version, record, record_capacity);
    case 6U:
        if (record_capacity < 4U) {
            return 0U;
        }
        record[0] = RETRO_VAULT_INFO_YEAR;
        record[1] = 2U;
        write_u16(record + 2U, package->release_year);
        return 4U;
    case 7U:
        if (record_capacity < 3U) {
            return 0U;
        }
        record[0] = RETRO_VAULT_INFO_RATING;
        record[1] = 1U;
        record[2] = package->rating;
        return 3U;
    case 8U:
        return make_string_tlv(
            RETRO_VAULT_INFO_DESCRIPTION,
            package->description,
            record,
            record_capacity
        );
    default:
        if ((size_t)(cursor - 9U) >= package->download_count) {
            return 0U;
        }
        return make_download_tlv(
            &package->downloads[cursor - 9U],
            record,
            record_capacity
        );
    }
}

static int write_info_page(
    uint8_t opcode,
    const struct retro_vault_package *package,
    uint16_t cursor,
    uint8_t *response,
    size_t response_capacity
)
{
    size_t record_count = 9U + package->download_count;
    size_t output_size = retro_vault_page_header_size;
    uint8_t tlv_count = 0U;
    uint16_t next_cursor = RETRO_VAULT_CURSOR_END;

    if ((response_capacity < retro_vault_page_header_size) ||
        ((size_t)cursor > record_count)) {
        return write_error_response(
            opcode,
            RETRO_VAULT_STATUS_BAD_REQUEST,
            response,
            response_capacity
        );
    }

    while ((size_t)cursor < record_count) {
        uint8_t record[249];
        size_t record_size = make_info_record(
            package,
            cursor,
            record,
            sizeof(record)
        );

        if (record_size == 0U) {
            return write_error_response(
                opcode,
                RETRO_VAULT_STATUS_INTERNAL_ERROR,
                response,
                response_capacity
            );
        }
        if (record_size > response_capacity - output_size) {
            if (tlv_count == 0U) {
                record_size = make_info_record(
                    package,
                    cursor,
                    record,
                    response_capacity - output_size
                );
                if (record_size == 0U) {
                    return write_error_response(
                        opcode,
                        RETRO_VAULT_STATUS_TOO_LARGE,
                        response,
                        response_capacity
                    );
                }
            } else {
                next_cursor = cursor;
                break;
            }
        }

        memcpy(response + output_size, record, record_size);
        output_size += record_size;
        ++tlv_count;
        ++cursor;
    }

    response[0] = (uint8_t)(opcode | RETRO_VAULT_RESPONSE_BIT);
    response[1] = RETRO_VAULT_STATUS_OK;
    write_u16(response + 2U, next_cursor);
    response[4] = tlv_count;
    return (int)output_size;
}

static int identifier_is_safe(const uint8_t *identifier, size_t size)
{
    size_t index = 0U;

    if ((identifier == NULL) || (size == 0U)) {
        return 0;
    }
    for (index = 0U; index < size; ++index) {
        unsigned char character = identifier[index];
        if (!(((character >= 'a') && (character <= 'z')) ||
              ((character >= 'A') && (character <= 'Z')) ||
              ((character >= '0') && (character <= '9')) ||
              (character == '-'))) {
            return 0;
        }
    }
    return 1;
}

static char *duplicate_bytes(const uint8_t *data, size_t size)
{
    char *text = malloc(size + 1U);
    if (text == NULL) {
        return NULL;
    }
    memcpy(text, data, size);
    text[size] = '\0';
    return text;
}

static uint8_t ensure_download(
    struct retro_vault_context *context,
    const uint8_t *package_id,
    size_t package_id_size,
    const uint8_t *download_id,
    size_t download_id_size
)
{
    char *package_text = NULL;
    char *download_text = NULL;
    char path[600];
    struct retro_vault_buffer response;
    long status_code = 0L;
    int result = 0;

    if ((context->download_package_id != NULL) &&
        (context->download_id != NULL) &&
        (strlen(context->download_package_id) == package_id_size) &&
        (strlen(context->download_id) == download_id_size) &&
        (memcmp(context->download_package_id, package_id, package_id_size) == 0) &&
        (memcmp(context->download_id, download_id, download_id_size) == 0)) {
        return RETRO_VAULT_STATUS_OK;
    }

    if (!identifier_is_safe(package_id, package_id_size) ||
        !identifier_is_safe(download_id, download_id_size)) {
        return RETRO_VAULT_STATUS_BAD_REQUEST;
    }

    package_text = duplicate_bytes(package_id, package_id_size);
    download_text = duplicate_bytes(download_id, download_id_size);
    if ((package_text == NULL) || (download_text == NULL)) {
        free(package_text);
        free(download_text);
        return RETRO_VAULT_STATUS_INTERNAL_ERROR;
    }

    if (snprintf(
        path,
        sizeof(path),
        "/api/v1/catalog/packages/%s/downloads/%s",
        package_text,
        download_text
    ) >= (int)sizeof(path)) {
        free(package_text);
        free(download_text);
        return RETRO_VAULT_STATUS_BAD_REQUEST;
    }

    retro_vault_buffer_init(&response);
    result = context->http.get(
        context->http.user_data,
        path,
        retro_vault_download_limit,
        &response,
        &status_code
    );
    if ((result != retro_vault_http_ok) ||
        (status_code < 200L) || (status_code >= 300L)) {
        retro_vault_buffer_free(&response);
        free(package_text);
        free(download_text);
        return http_error_status(result, status_code);
    }

    retro_vault_buffer_free(&context->download_data);
    free(context->download_package_id);
    free(context->download_id);
    context->download_data = response;
    context->download_package_id = package_text;
    context->download_id = download_text;
    return RETRO_VAULT_STATUS_OK;
}

static int handle_capabilities(
    uint8_t opcode,
    uint8_t *response,
    size_t response_capacity
)
{
    if (response_capacity < 7U) {
        return -1;
    }

    response[0] = (uint8_t)(opcode | RETRO_VAULT_RESPONSE_BIT);
    response[1] = RETRO_VAULT_STATUS_OK;
    response[2] = RETRO_VAULT_PROTOCOL_VERSION;
    response[3] = RETRO_VAULT_FEATURE_LIST |
        RETRO_VAULT_FEATURE_SEARCH |
        RETRO_VAULT_FEATURE_INFO |
        RETRO_VAULT_FEATURE_DOWNLOAD;
    response[4] = (uint8_t)retro_vault_page_header_size;
    response[5] = (uint8_t)retro_vault_download_header_size;
    response[6] = response_capacity > retro_vault_download_header_size
        ? (uint8_t)(response_capacity - retro_vault_download_header_size)
        : 0U;
    return 7;
}

static int handle_list_or_search(
    struct retro_vault_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint16_t cursor = 0U;
    uint8_t platform_size = 0U;
    const uint8_t *platform = NULL;
    const uint8_t *query = NULL;
    uint8_t query_size = 0U;
    size_t offset = 4U;
    uint8_t status = RETRO_VAULT_STATUS_OK;

    if (request_size < 4U) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    cursor = read_u16(request + 1U);
    if (cursor == RETRO_VAULT_CURSOR_END) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    platform_size = request[3];
    if ((size_t)platform_size > request_size - offset) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }
    platform = request + offset;
    offset += platform_size;

    if (opcode == RETRO_VAULT_OP_SEARCH) {
        if (offset >= request_size) {
            return write_error_response(
                opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
        }
        query_size = request[offset++];
        if ((query_size == 0U) || ((size_t)query_size != request_size - offset)) {
            return write_error_response(
                opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
        }
        query = request + offset;
    } else if (offset != request_size) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    status = ensure_catalog(context);
    if (status != RETRO_VAULT_STATUS_OK) {
        return write_error_response(opcode, status, response, response_capacity);
    }

    return write_catalog_page(
        context,
        opcode,
        cursor,
        platform,
        platform_size,
        query,
        query_size,
        response,
        response_capacity
    );
}

static int handle_info(
    struct retro_vault_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint16_t cursor = 0U;
    uint8_t id_size = 0U;
    const struct retro_vault_package *package = NULL;
    uint8_t status = RETRO_VAULT_STATUS_OK;

    if (request_size < 4U) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    cursor = read_u16(request + 1U);
    id_size = request[3];
    if ((id_size == 0U) || ((size_t)id_size != request_size - 4U)) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    status = ensure_catalog(context);
    if (status != RETRO_VAULT_STATUS_OK) {
        return write_error_response(opcode, status, response, response_capacity);
    }

    package = find_package(context, request + 4U, id_size);
    if (package == NULL) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_NOT_FOUND, response, response_capacity);
    }

    return write_info_page(opcode, package, cursor, response, response_capacity);
}

static int handle_download(
    struct retro_vault_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint32_t offset = 0U;
    uint8_t maximum_bytes = 0U;
    uint8_t package_id_size = 0U;
    uint8_t download_id_size = 0U;
    const uint8_t *package_id = NULL;
    const uint8_t *download_id = NULL;
    size_t cursor = 7U;
    size_t available = 0U;
    size_t data_size = 0U;
    uint8_t status = RETRO_VAULT_STATUS_OK;

    if ((request_size < 8U) ||
        (response_capacity < retro_vault_download_header_size)) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    offset = read_u32(request + 1U);
    maximum_bytes = request[5];
    package_id_size = request[6];
    if ((package_id_size == 0U) ||
        ((size_t)package_id_size > request_size - cursor)) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }
    package_id = request + cursor;
    cursor += package_id_size;

    if (cursor >= request_size) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }
    download_id_size = request[cursor++];
    if ((download_id_size == 0U) ||
        ((size_t)download_id_size != request_size - cursor)) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }
    download_id = request + cursor;

    status = ensure_download(
        context,
        package_id,
        package_id_size,
        download_id,
        download_id_size
    );
    if (status != RETRO_VAULT_STATUS_OK) {
        return write_error_response(opcode, status, response, response_capacity);
    }

    if ((uint64_t)context->download_data.size > UINT32_MAX) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_TOO_LARGE, response, response_capacity);
    }
    if ((size_t)offset > context->download_data.size) {
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }

    available = context->download_data.size - (size_t)offset;
    data_size = response_capacity - retro_vault_download_header_size;
    if ((maximum_bytes > 0U) && (data_size > maximum_bytes)) {
        data_size = maximum_bytes;
    }
    if (data_size > UINT8_MAX) {
        data_size = UINT8_MAX;
    }
    if (data_size > available) {
        data_size = available;
    }

    response[0] = (uint8_t)(opcode | RETRO_VAULT_RESPONSE_BIT);
    response[1] = RETRO_VAULT_STATUS_OK;
    write_u32(response + 2U, offset);
    write_u32(response + 6U, (uint32_t)context->download_data.size);
    response[10] = (uint8_t)data_size;
    if (data_size > 0U) {
        memcpy(
            response + retro_vault_download_header_size,
            context->download_data.data + offset,
            data_size
        );
    }
    return (int)(retro_vault_download_header_size + data_size);
}

int handle_retro_vault_request(
    struct retro_vault_context *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint8_t opcode = 0U;

    if ((context == NULL) || (request == NULL) || (request_size == 0U) ||
        (response == NULL) || (response_capacity < 2U)) {
        return -1;
    }

    opcode = request[0];
    switch (opcode) {
    case RETRO_VAULT_OP_CAPABILITIES:
        if (request_size != 1U) {
            return write_error_response(
                opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
        }
        return handle_capabilities(opcode, response, response_capacity);
    case RETRO_VAULT_OP_LIST:
    case RETRO_VAULT_OP_SEARCH:
        return handle_list_or_search(
            context,
            opcode,
            request,
            request_size,
            response,
            response_capacity
        );
    case RETRO_VAULT_OP_INFO:
        return handle_info(
            context,
            opcode,
            request,
            request_size,
            response,
            response_capacity
        );
    case RETRO_VAULT_OP_DOWNLOAD:
        return handle_download(
            context,
            opcode,
            request,
            request_size,
            response,
            response_capacity
        );
    default:
        return write_error_response(
            opcode, RETRO_VAULT_STATUS_BAD_REQUEST, response, response_capacity);
    }
}
