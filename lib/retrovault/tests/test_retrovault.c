#include "retrovault_core.h"
#include "retrovault_curl.h"
#include "squid_server/retrovault_protocol.h"

#include <stdio.h>
#include <string.h>

#define test_packet_capacity 255U

static const char catalog_json[] =
    "["
    "{"
      "\"id\":\"manic-miner\","
      "\"name\":\"Manic Miner\","
      "\"vendor\":\"Software Projects\","
      "\"platformId\":\"zxs\","
      "\"platformName\":\"ZX Spectrum\","
      "\"modelId\":null,"
      "\"modelName\":null,"
      "\"releaseYear\":1983,"
      "\"version\":\"1.0\","
      "\"rating\":\"Great\","
      "\"description\":\"Fast arcade action\","
      "\"files\":[],"
      "\"downloads\":[{"
        "\"id\":\"complete\","
        "\"label\":\"Cassette image\","
        "\"format\":\"TZX\","
        "\"downloadUrl\":\"/api/v1/catalog/packages/zxs/manic-miner/downloads/complete\","
        "\"files\":[{"
          "\"fileName\":\"MANIC.TZX\","
          "\"downloadUrl\":\"/api/content/zxs/manic-miner/MANIC.TZX\","
          "\"sizeBytes\":600,"
          "\"sha256\":null"
        "}]"
      "}],"
      "\"documents\":[],"
      "\"screenshotUrls\":[]"
    "},"
    "{"
      "\"id\":\"lunatik\","
      "\"name\":\"\\u0160ah\","
      "\"vendor\":\"Iskra Delta\","
      "\"platformId\":\"idp\","
      "\"platformName\":\"Iskra Delta Partner\","
      "\"modelId\":\"gdp\","
      "\"modelName\":\"GDP\","
      "\"releaseYear\":2024,"
      "\"version\":\"1.0\","
      "\"rating\":\"Legendary\","
      "\"description\":\"Chess for the Partner\","
      "\"files\":[],"
      "\"downloads\":[{"
        "\"id\":\"complete\","
        "\"label\":\"Program file\","
        "\"format\":\"COM\","
        "\"downloadUrl\":\"/api/v1/catalog/packages/idp/gdp/lunatik/downloads/complete\","
        "\"files\":[{"
          "\"fileName\":\"LUNATIK.COM\","
          "\"downloadUrl\":\"/api/content/idp/gdp/lunatik/LUNATIK.COM\","
          "\"sizeBytes\":32,"
          "\"sha256\":null"
        "}]"
      "}],"
      "\"documents\":[],"
      "\"screenshotUrls\":[]"
    "}"
    "]";

struct mock_http_state {
    unsigned int catalog_calls;
    unsigned int download_calls;
};

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

static int mock_http_get(
    void *user_data,
    const char *path,
    size_t size_limit,
    struct retro_vault_buffer *response,
    long *status_code
)
{
    struct mock_http_state *state = user_data;

    response->limit = size_limit;
    if (strcmp(path, "/api/v1/catalog/packages") == 0) {
        ++state->catalog_calls;
        *status_code = 200L;
        return retro_vault_buffer_append(
            response,
            catalog_json,
            sizeof(catalog_json) - 1U
        ) == 0 ? retro_vault_http_ok : retro_vault_http_error;
    }

    if (strcmp(
        path,
        "/api/v1/catalog/packages/zxs/manic-miner/downloads/complete"
    ) == 0) {
        uint8_t data[600];
        size_t index = 0U;

        ++state->download_calls;
        for (index = 0U; index < sizeof(data); ++index) {
            data[index] = (uint8_t)index;
        }
        *status_code = 200L;
        return retro_vault_buffer_append(response, data, sizeof(data)) == 0
            ? retro_vault_http_ok
            : retro_vault_http_error;
    }

    if (strcmp(
        path,
        "/api/v1/catalog/packages/idp/gdp/lunatik/downloads/complete"
    ) == 0) {
        uint8_t data[32];
        size_t index = 0U;

        ++state->download_calls;
        for (index = 0U; index < sizeof(data); ++index) {
            data[index] = (uint8_t)(0xA0U + index);
        }
        *status_code = 200L;
        return retro_vault_buffer_append(response, data, sizeof(data)) == 0
            ? retro_vault_http_ok
            : retro_vault_http_error;
    }

    if (strcmp(path, "/api/v1/catalog/packages/lunatik/downloads/complete") == 0) {
        *status_code = 409L;
        return retro_vault_http_ok;
    }

    *status_code = 404L;
    return retro_vault_http_ok;
}

static int test_capabilities(struct retro_vault_context *context)
{
    const uint8_t request[] = { RETRO_VAULT_OP_CAPABILITIES };
    uint8_t response[test_packet_capacity];
    int size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));

    if ((size != 7) ||
        (response[0] != (RETRO_VAULT_OP_CAPABILITIES | RETRO_VAULT_RESPONSE_BIT)) ||
        (response[1] != RETRO_VAULT_STATUS_OK) ||
        (response[2] != RETRO_VAULT_PROTOCOL_VERSION) ||
        ((response[3] & RETRO_VAULT_FEATURE_DOWNLOAD) == 0U) ||
        (response[6] != 244U)) {
        return 1;
    }
    return 0;
}

static int parse_list_entry(
    const uint8_t *response,
    size_t response_size,
    size_t *offset,
    const char *expected_id,
    const char *expected_name
)
{
    uint8_t id_size = 0U;
    uint8_t name_size = 0U;

    if (*offset >= response_size) {
        return -1;
    }
    id_size = response[(*offset)++];
    if ((size_t)id_size > response_size - *offset ||
        (strlen(expected_id) != id_size) ||
        (memcmp(response + *offset, expected_id, id_size) != 0)) {
        return -1;
    }
    *offset += id_size;

    if (*offset >= response_size) {
        return -1;
    }
    name_size = response[(*offset)++];
    if ((size_t)name_size > response_size - *offset ||
        (strlen(expected_name) != name_size) ||
        (memcmp(response + *offset, expected_name, name_size) != 0)) {
        return -1;
    }
    *offset += name_size;
    return 0;
}

static int test_list(
    struct retro_vault_context *context,
    struct mock_http_state *http_state
)
{
    const uint8_t request[] = { RETRO_VAULT_OP_LIST, 0U, 0U, 0U };
    uint8_t response[test_packet_capacity];
    size_t offset = 5U;
    int size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));

    if ((size <= 5) || (response[1] != RETRO_VAULT_STATUS_OK) ||
        (read_u16(response + 2U) != RETRO_VAULT_CURSOR_END) ||
        (response[4] != 2U) ||
        (parse_list_entry(response, (size_t)size, &offset, "manic-miner", "Manic Miner") != 0) ||
        (parse_list_entry(response, (size_t)size, &offset, "lunatik", "\xC5\xA0" "ah") != 0) ||
        (offset != (size_t)size) || (http_state->catalog_calls != 1U)) {
        return 1;
    }
    return 0;
}

static int test_filtered_list(struct retro_vault_context *context)
{
    static const char platform[] = "idp";
    uint8_t request[4U + sizeof(platform) - 1U];
    uint8_t response[test_packet_capacity];
    size_t offset = 5U;
    int size = 0;

    request[0] = RETRO_VAULT_OP_LIST;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = (uint8_t)(sizeof(platform) - 1U);
    memcpy(request + 4U, platform, sizeof(platform) - 1U);

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size <= 5) || (response[4] != 1U) ||
        (parse_list_entry(
            response,
            (size_t)size,
            &offset,
            "lunatik",
            "\xC5\xA0" "ah"
        ) != 0)) {
        return 1;
    }
    return 0;
}

static int test_legacy_platform_list(struct retro_vault_context *context)
{
    static const char platform[] = "iskra-delta-partner";
    uint8_t request[4U + sizeof(platform) - 1U];
    uint8_t response[test_packet_capacity];
    size_t offset = 5U;
    int size = 0;

    request[0] = RETRO_VAULT_OP_LIST;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = (uint8_t)(sizeof(platform) - 1U);
    memcpy(request + 4U, platform, sizeof(platform) - 1U);

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size <= 5) || (response[4] != 1U) ||
        (parse_list_entry(
            response,
            (size_t)size,
            &offset,
            "lunatik",
            "\xC5\xA0" "ah"
        ) != 0)) {
        return 1;
    }
    return 0;
}

static int test_gdp_model_list(struct retro_vault_context *context)
{
    static const char platform[] = "idp";
    static const char model[] = "gdp";
    uint8_t request[5U + sizeof(platform) - 1U + sizeof(model) - 1U];
    uint8_t response[test_packet_capacity];
    size_t offset = 5U;
    int size = 0;

    request[0] = RETRO_VAULT_OP_LIST;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = (uint8_t)(sizeof(platform) - 1U);
    memcpy(request + 4U, platform, sizeof(platform) - 1U);
    request[4U + sizeof(platform) - 1U] = (uint8_t)(sizeof(model) - 1U);
    memcpy(
        request + 5U + sizeof(platform) - 1U,
        model,
        sizeof(model) - 1U
    );

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size <= 5) || (response[4] != 1U) ||
        (parse_list_entry(
            response,
            (size_t)size,
            &offset,
            "lunatik",
            "\xC5\xA0" "ah"
        ) != 0)) {
        return 1;
    }
    return 0;
}

static int test_p_model_list(struct retro_vault_context *context)
{
    static const char platform[] = "idp";
    static const char model[] = "p";
    uint8_t request[5U + sizeof(platform) - 1U + sizeof(model) - 1U];
    uint8_t response[test_packet_capacity];
    int size = 0;

    request[0] = RETRO_VAULT_OP_LIST;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = (uint8_t)(sizeof(platform) - 1U);
    memcpy(request + 4U, platform, sizeof(platform) - 1U);
    request[4U + sizeof(platform) - 1U] = (uint8_t)(sizeof(model) - 1U);
    memcpy(
        request + 5U + sizeof(platform) - 1U,
        model,
        sizeof(model) - 1U
    );

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    return ((size == 5) &&
        (response[1] == RETRO_VAULT_STATUS_OK) &&
        (response[4] == 0U)) ? 0 : 1;
}

static int test_search(struct retro_vault_context *context)
{
    static const char query[] = "iskra";
    uint8_t request[5U + sizeof(query) - 1U];
    uint8_t response[test_packet_capacity];
    size_t offset = 5U;
    int size = 0;

    request[0] = RETRO_VAULT_OP_SEARCH;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = 0U;
    request[4] = (uint8_t)(sizeof(query) - 1U);
    memcpy(request + 5U, query, sizeof(query) - 1U);

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size <= 5) || (response[4] != 1U) ||
        (parse_list_entry(
            response,
            (size_t)size,
            &offset,
            "lunatik",
            "\xC5\xA0" "ah"
        ) != 0)) {
        return 1;
    }
    return 0;
}

static int test_info(struct retro_vault_context *context)
{
    static const char package_id[] = "manic-miner";
    uint8_t request[4U + sizeof(package_id) - 1U];
    uint8_t response[test_packet_capacity];
    uint16_t cursor = 0U;
    int saw_year = 0;
    int saw_download = 0;
    unsigned int page = 0U;

    request[0] = RETRO_VAULT_OP_INFO;
    request[3] = (uint8_t)(sizeof(package_id) - 1U);
    memcpy(request + 4U, package_id, sizeof(package_id) - 1U);

    for (page = 0U; page < 8U; ++page) {
        size_t offset = 5U;
        uint8_t index = 0U;
        int size = 0;

        request[1] = (uint8_t)(cursor & 0xFFU);
        request[2] = (uint8_t)(cursor >> 8U);
        size = handle_retro_vault_request(
            context, request, sizeof(request), response, sizeof(response));
        if ((size < 5) || (response[1] != RETRO_VAULT_STATUS_OK)) {
            return 1;
        }

        for (index = 0U; index < response[4]; ++index) {
            uint8_t type = 0U;
            uint8_t value_size = 0U;
            if (offset + 2U > (size_t)size) {
                return 1;
            }
            type = response[offset++];
            value_size = response[offset++];
            if ((size_t)value_size > (size_t)size - offset) {
                return 1;
            }
            if ((type == RETRO_VAULT_INFO_YEAR) &&
                (value_size == 2U) && (read_u16(response + offset) == 1983U)) {
                saw_year = 1;
            }
            if ((type == RETRO_VAULT_INFO_DOWNLOAD) && (value_size > 8U)) {
                saw_download = 1;
            }
            offset += value_size;
        }

        cursor = read_u16(response + 2U);
        if (cursor == RETRO_VAULT_CURSOR_END) {
            break;
        }
    }

    return (saw_year && saw_download &&
            (cursor == RETRO_VAULT_CURSOR_END)) ? 0 : 1;
}

static int test_download(
    struct retro_vault_context *context,
    struct mock_http_state *http_state
)
{
    static const char package_id[] = "manic-miner";
    static const char download_id[] = "complete";
    uint8_t request[
        8U + sizeof(package_id) - 1U + sizeof(download_id) - 1U
    ];
    uint8_t response[64];
    size_t cursor = 7U;
    int size = 0;

    request[0] = RETRO_VAULT_OP_DOWNLOAD;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = 0U;
    request[4] = 0U;
    request[5] = 20U;
    request[6] = (uint8_t)(sizeof(package_id) - 1U);
    memcpy(request + cursor, package_id, sizeof(package_id) - 1U);
    cursor += sizeof(package_id) - 1U;
    request[cursor++] = (uint8_t)(sizeof(download_id) - 1U);
    memcpy(request + cursor, download_id, sizeof(download_id) - 1U);

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size != 31) || (response[1] != RETRO_VAULT_STATUS_OK) ||
        (read_u32(response + 2U) != 0U) ||
        (read_u32(response + 6U) != 600U) ||
        (response[10] != 20U) || (response[11] != 0U) ||
        (response[30] != 19U) || (http_state->download_calls != 1U)) {
        return 1;
    }

    request[1] = 20U;
    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size != 31) || (read_u32(response + 2U) != 20U) ||
        (response[11] != 20U) || (response[30] != 39U) ||
        (http_state->download_calls != 1U)) {
        return 1;
    }
    return 0;
}

static int test_model_download(
    struct retro_vault_context *context,
    struct mock_http_state *http_state
)
{
    static const char package_id[] = "lunatik";
    static const char download_id[] = "complete";
    uint8_t request[
        8U + sizeof(package_id) - 1U + sizeof(download_id) - 1U
    ];
    uint8_t response[64];
    size_t cursor = 7U;
    int size = 0;

    request[0] = RETRO_VAULT_OP_DOWNLOAD;
    request[1] = 0U;
    request[2] = 0U;
    request[3] = 0U;
    request[4] = 0U;
    request[5] = 16U;
    request[6] = (uint8_t)(sizeof(package_id) - 1U);
    memcpy(request + cursor, package_id, sizeof(package_id) - 1U);
    cursor += sizeof(package_id) - 1U;
    request[cursor++] = (uint8_t)(sizeof(download_id) - 1U);
    memcpy(request + cursor, download_id, sizeof(download_id) - 1U);

    size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));
    if ((size != 27) || (response[1] != RETRO_VAULT_STATUS_OK) ||
        (read_u32(response + 2U) != 0U) ||
        (read_u32(response + 6U) != 32U) ||
        (response[10] != 16U) || (response[11] != 0xA0U) ||
        (response[26] != 0xAFU) || (http_state->download_calls != 2U)) {
        return 1;
    }
    return 0;
}

static int test_bad_request(struct retro_vault_context *context)
{
    const uint8_t request[] = { RETRO_VAULT_OP_INFO };
    uint8_t response[16];
    int size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));

    return (size == 2) &&
        (response[1] == RETRO_VAULT_STATUS_BAD_REQUEST) ? 0 : 1;
}

static int test_unsafe_download_id(struct retro_vault_context *context)
{
    const uint8_t request[] = {
        RETRO_VAULT_OP_DOWNLOAD,
        0U, 0U, 0U, 0U,
        0U,
        2U, '.', '.',
        8U, 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e'
    };
    uint8_t response[16];
    int size = handle_retro_vault_request(
        context, request, sizeof(request), response, sizeof(response));

    return (size == 2) &&
        (response[1] == RETRO_VAULT_STATUS_BAD_REQUEST) ? 0 : 1;
}

static int run_live_test(void)
{
    const uint8_t request[] = { RETRO_VAULT_OP_LIST, 0U, 0U, 0U };
    struct retro_vault_curl_client curl;
    struct retro_vault_http_client http;
    struct retro_vault_context context;
    const struct retro_vault_package *selected_package = NULL;
    const struct retro_vault_download *selected_download = NULL;
    uint32_t smallest_download = RETRO_VAULT_SIZE_UNKNOWN;
    uint8_t operation[test_packet_capacity];
    uint8_t response[test_packet_capacity];
    size_t package_index = 0U;
    size_t download_index = 0U;
    size_t package_id_size = 0U;
    size_t download_id_size = 0U;
    size_t operation_size = 0U;
    int response_size = 0;
    int result = 1;

    if (init_retro_vault_curl_client(&curl) != 0) {
        fputs("failed to initialize the Retro Vault HTTP client\n", stderr);
        return 1;
    }

    http.get = get_retro_vault_curl;
    http.user_data = &curl;
    init_retro_vault_context(&context, &http);
    response_size = handle_retro_vault_request(
        &context,
        request,
        sizeof(request),
        response,
        sizeof(response)
    );

    if ((response_size < 5) ||
        (response[0] != (RETRO_VAULT_OP_LIST | RETRO_VAULT_RESPONSE_BIT)) ||
        (response[1] != RETRO_VAULT_STATUS_OK) || (response[4] == 0U)) {
        fprintf(
            stderr,
            "live Retro Vault catalog request failed (size=%d, status=%u)\n",
            response_size,
            response_size >= 2 ? response[1] : UINT8_MAX
        );
        goto cleanup;
    }

    for (package_index = 0U;
         package_index < context.package_count;
         ++package_index) {
        for (download_index = 0U;
             download_index < context.packages[package_index].download_count;
             ++download_index) {
            const struct retro_vault_download *candidate =
                &context.packages[package_index].downloads[download_index];
            if ((candidate->aggregate_size > 0U) &&
                (candidate->aggregate_size < smallest_download)) {
                smallest_download = candidate->aggregate_size;
                selected_package = &context.packages[package_index];
                selected_download = candidate;
            }
        }
    }

    if ((selected_package == NULL) || (selected_download == NULL) ||
        (smallest_download > 1024U * 1024U)) {
        fputs("live catalog has no small download for the smoke test\n", stderr);
        goto cleanup;
    }

    package_id_size = strlen(selected_package->id);
    download_id_size = strlen(selected_download->id);
    if ((package_id_size == 0U) || (package_id_size > UINT8_MAX) ||
        (download_id_size == 0U) || (download_id_size > UINT8_MAX) ||
        (8U + package_id_size + download_id_size > sizeof(operation))) {
        fputs("live catalog contains an ID too large for the protocol\n", stderr);
        goto cleanup;
    }

    operation[0] = RETRO_VAULT_OP_INFO;
    operation[1] = 0U;
    operation[2] = 0U;
    operation[3] = (uint8_t)package_id_size;
    memcpy(operation + 4U, selected_package->id, package_id_size);
    response_size = handle_retro_vault_request(
        &context,
        operation,
        4U + package_id_size,
        response,
        sizeof(response)
    );
    if ((response_size < 5) ||
        (response[0] != (RETRO_VAULT_OP_INFO | RETRO_VAULT_RESPONSE_BIT)) ||
        (response[1] != RETRO_VAULT_STATUS_OK) || (response[4] == 0U)) {
        fputs("live Retro Vault INFO request failed\n", stderr);
        goto cleanup;
    }

    operation[0] = RETRO_VAULT_OP_DOWNLOAD;
    operation[1] = 0U;
    operation[2] = 0U;
    operation[3] = 0U;
    operation[4] = 0U;
    operation[5] = 1U;
    operation[6] = (uint8_t)package_id_size;
    memcpy(operation + 7U, selected_package->id, package_id_size);
    operation_size = 7U + package_id_size;
    operation[operation_size++] = (uint8_t)download_id_size;
    memcpy(operation + operation_size, selected_download->id, download_id_size);
    operation_size += download_id_size;

    response_size = handle_retro_vault_request(
        &context,
        operation,
        operation_size,
        response,
        sizeof(response)
    );
    if ((response_size != 12) ||
        (response[0] != (RETRO_VAULT_OP_DOWNLOAD | RETRO_VAULT_RESPONSE_BIT)) ||
        (response[1] != RETRO_VAULT_STATUS_OK) ||
        (read_u32(response + 2U) != 0U) ||
        (read_u32(response + 6U) == 0U) || (response[10] != 1U)) {
        fputs("live Retro Vault DOWNLOAD request failed\n", stderr);
        goto cleanup;
    }

    printf(
        "live Retro Vault LIST, INFO, and DOWNLOAD passed (%s/%s)\n",
        selected_package->id,
        selected_download->id
    );
    result = 0;

cleanup:
    free_retro_vault_context(&context);
    free_retro_vault_curl_client(&curl);
    return result;
}

int main(int argc, char **argv)
{
    struct mock_http_state http_state = { 0U, 0U };
    struct retro_vault_http_client http = { mock_http_get, &http_state };
    struct retro_vault_context context;
    int failed = 0;

    if ((argc == 2) && (strcmp(argv[1], "--live") == 0)) {
        return run_live_test();
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--live]\n", argv[0]);
        return 2;
    }

    init_retro_vault_context(&context, &http);

#define RUN_TEST(test_call) \
    do { \
        if ((test_call) != 0) { \
            fprintf(stderr, "%s failed\n", #test_call); \
            failed = 1; \
        } \
    } while (0)

    RUN_TEST(test_capabilities(&context));
    RUN_TEST(test_list(&context, &http_state));
    RUN_TEST(test_filtered_list(&context));
    RUN_TEST(test_legacy_platform_list(&context));
    RUN_TEST(test_gdp_model_list(&context));
    RUN_TEST(test_p_model_list(&context));
    RUN_TEST(test_search(&context));
    RUN_TEST(test_info(&context));
    RUN_TEST(test_download(&context, &http_state));
    RUN_TEST(test_model_download(&context, &http_state));
    RUN_TEST(test_bad_request(&context));
    RUN_TEST(test_unsafe_download_id(&context));

#undef RUN_TEST

    free_retro_vault_context(&context);
    if (failed) {
        return 1;
    }

    puts("all retrovault tests passed");
    return 0;
}
