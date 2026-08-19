#include "retrovault_json.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define retro_vault_json_max_depth 32U

static const char *skip_space(const char *cursor, const char *end)
{
    while ((cursor < end) && isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    return cursor;
}

static int parse_string_end(
    const char *cursor,
    const char *end,
    const char **after
)
{
    if ((cursor >= end) || (*cursor != '"')) {
        return -1;
    }

    ++cursor;
    while (cursor < end) {
        unsigned char character = (unsigned char)*cursor++;

        if (character == '"') {
            *after = cursor;
            return 0;
        }

        if (character < 0x20U) {
            return -1;
        }

        if (character == '\\') {
            if (cursor >= end) {
                return -1;
            }

            character = (unsigned char)*cursor++;
            if (strchr("\"\\/bfnrt", (int)character) != NULL) {
                continue;
            }

            if (character == 'u') {
                size_t index = 0U;
                if ((size_t)(end - cursor) < 4U) {
                    return -1;
                }
                for (index = 0U; index < 4U; ++index) {
                    if (!isxdigit((unsigned char)cursor[index])) {
                        return -1;
                    }
                }
                cursor += 4;
                continue;
            }

            return -1;
        }
    }

    return -1;
}

static int parse_value_internal(
    const char *cursor,
    const char *end,
    struct retro_vault_json_value *value,
    const char **after,
    unsigned int depth
)
{
    const char *start = skip_space(cursor, end);
    const char *current = start;

    if ((start >= end) || (depth > retro_vault_json_max_depth)) {
        return -1;
    }

    if (*current == '"') {
        if (parse_string_end(current, end, after) != 0) {
            return -1;
        }
        value->start = start;
        value->end = *after;
        value->type = retro_vault_json_string;
        return 0;
    }

    if (*current == '{') {
        ++current;
        current = skip_space(current, end);
        if ((current < end) && (*current == '}')) {
            value->start = start;
            value->end = current + 1;
            value->type = retro_vault_json_object;
            *after = current + 1;
            return 0;
        }

        for (;;) {
            const char *key_end = NULL;
            struct retro_vault_json_value child;

            current = skip_space(current, end);
            if (parse_string_end(current, end, &key_end) != 0) {
                return -1;
            }
            current = skip_space(key_end, end);
            if ((current >= end) || (*current != ':')) {
                return -1;
            }
            ++current;

            if (parse_value_internal(
                current,
                end,
                &child,
                &current,
                depth + 1U
            ) != 0) {
                return -1;
            }

            current = skip_space(current, end);
            if ((current < end) && (*current == ',')) {
                ++current;
                continue;
            }
            if ((current < end) && (*current == '}')) {
                value->start = start;
                value->end = current + 1;
                value->type = retro_vault_json_object;
                *after = current + 1;
                return 0;
            }
            return -1;
        }
    }

    if (*current == '[') {
        ++current;
        current = skip_space(current, end);
        if ((current < end) && (*current == ']')) {
            value->start = start;
            value->end = current + 1;
            value->type = retro_vault_json_array;
            *after = current + 1;
            return 0;
        }

        for (;;) {
            struct retro_vault_json_value child;
            if (parse_value_internal(
                current,
                end,
                &child,
                &current,
                depth + 1U
            ) != 0) {
                return -1;
            }

            current = skip_space(current, end);
            if ((current < end) && (*current == ',')) {
                ++current;
                continue;
            }
            if ((current < end) && (*current == ']')) {
                value->start = start;
                value->end = current + 1;
                value->type = retro_vault_json_array;
                *after = current + 1;
                return 0;
            }
            return -1;
        }
    }

    if (((size_t)(end - current) >= 4U) &&
        (memcmp(current, "true", 4U) == 0)) {
        value->start = start;
        value->end = current + 4;
        value->type = retro_vault_json_boolean;
        *after = current + 4;
        return 0;
    }

    if (((size_t)(end - current) >= 5U) &&
        (memcmp(current, "false", 5U) == 0)) {
        value->start = start;
        value->end = current + 5;
        value->type = retro_vault_json_boolean;
        *after = current + 5;
        return 0;
    }

    if (((size_t)(end - current) >= 4U) &&
        (memcmp(current, "null", 4U) == 0)) {
        value->start = start;
        value->end = current + 4;
        value->type = retro_vault_json_null;
        *after = current + 4;
        return 0;
    }

    if ((*current == '-') || isdigit((unsigned char)*current)) {
        ++current;
        while ((current < end) &&
               (strchr("0123456789.eE+-", (int)*current) != NULL)) {
            ++current;
        }
        value->start = start;
        value->end = current;
        value->type = retro_vault_json_number;
        *after = current;
        return 0;
    }

    return -1;
}

int parse_retro_vault_json(
    const char *data,
    size_t size,
    struct retro_vault_json_value *value
)
{
    const char *after = NULL;
    const char *end = NULL;

    if ((data == NULL) || (value == NULL)) {
        return -1;
    }

    end = data + size;
    if (parse_value_internal(data, end, value, &after, 0U) != 0) {
        return -1;
    }

    return skip_space(after, end) == end ? 0 : -1;
}

static int json_string_matches(
    const struct retro_vault_json_value *value,
    const char *text
)
{
    char *decoded = NULL;
    int matches = 0;

    decoded = duplicate_retro_vault_json_string(value);
    if (decoded == NULL) {
        return 0;
    }

    matches = strcmp(decoded, text) == 0;
    free(decoded);
    return matches;
}

int get_retro_vault_json_member(
    const struct retro_vault_json_value *object,
    const char *name,
    struct retro_vault_json_value *value
)
{
    const char *cursor = NULL;
    const char *object_end = NULL;

    if ((object == NULL) || (name == NULL) || (value == NULL) ||
        (object->type != retro_vault_json_object)) {
        return -1;
    }

    cursor = object->start + 1;
    object_end = object->end - 1;

    for (;;) {
        const char *after = NULL;
        struct retro_vault_json_value key;
        struct retro_vault_json_value member;

        cursor = skip_space(cursor, object_end);
        if (cursor >= object_end) {
            return 0;
        }

        if (parse_value_internal(
            cursor,
            object_end,
            &key,
            &after,
            0U
        ) != 0 || key.type != retro_vault_json_string) {
            return -1;
        }

        cursor = skip_space(after, object_end);
        if ((cursor >= object_end) || (*cursor != ':')) {
            return -1;
        }
        ++cursor;

        if (parse_value_internal(
            cursor,
            object_end,
            &member,
            &after,
            0U
        ) != 0) {
            return -1;
        }

        if (json_string_matches(&key, name)) {
            *value = member;
            return 1;
        }

        cursor = skip_space(after, object_end);
        if ((cursor < object_end) && (*cursor == ',')) {
            ++cursor;
            continue;
        }
        if (cursor == object_end) {
            return 0;
        }
        return -1;
    }
}

int init_retro_vault_json_array_iterator(
    const struct retro_vault_json_value *array,
    struct retro_vault_json_iterator *iterator
)
{
    if ((array == NULL) || (iterator == NULL) ||
        (array->type != retro_vault_json_array)) {
        return -1;
    }

    iterator->cursor = array->start + 1;
    iterator->end = array->end - 1;
    iterator->finished = 0;
    return 0;
}

int next_retro_vault_json_array_value(
    struct retro_vault_json_iterator *iterator,
    struct retro_vault_json_value *value
)
{
    const char *after = NULL;

    if ((iterator == NULL) || (value == NULL) || iterator->finished) {
        return 0;
    }

    iterator->cursor = skip_space(iterator->cursor, iterator->end);
    if (iterator->cursor >= iterator->end) {
        iterator->finished = 1;
        return 0;
    }

    if (parse_value_internal(
        iterator->cursor,
        iterator->end,
        value,
        &after,
        0U
    ) != 0) {
        return -1;
    }

    iterator->cursor = skip_space(after, iterator->end);
    if (iterator->cursor < iterator->end) {
        if (*iterator->cursor != ',') {
            return -1;
        }
        ++iterator->cursor;
    } else {
        iterator->finished = 1;
    }

    return 1;
}

static int hex_value(unsigned char character)
{
    if ((character >= '0') && (character <= '9')) {
        return (int)(character - '0');
    }
    if ((character >= 'a') && (character <= 'f')) {
        return (int)(character - 'a') + 10;
    }
    if ((character >= 'A') && (character <= 'F')) {
        return (int)(character - 'A') + 10;
    }
    return -1;
}

static int decode_hex_quad(const char *text, uint32_t *codepoint)
{
    size_t index = 0U;
    uint32_t value = 0U;

    for (index = 0U; index < 4U; ++index) {
        int digit = hex_value((unsigned char)text[index]);
        if (digit < 0) {
            return -1;
        }
        value = (value << 4U) | (uint32_t)digit;
    }

    *codepoint = value;
    return 0;
}

static size_t encode_utf8(uint32_t codepoint, char *output)
{
    if (codepoint <= 0x7FU) {
        output[0] = (char)codepoint;
        return 1U;
    }
    if (codepoint <= 0x7FFU) {
        output[0] = (char)(0xC0U | (codepoint >> 6U));
        output[1] = (char)(0x80U | (codepoint & 0x3FU));
        return 2U;
    }
    if (codepoint <= 0xFFFFU) {
        output[0] = (char)(0xE0U | (codepoint >> 12U));
        output[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        output[2] = (char)(0x80U | (codepoint & 0x3FU));
        return 3U;
    }

    output[0] = (char)(0xF0U | (codepoint >> 18U));
    output[1] = (char)(0x80U | ((codepoint >> 12U) & 0x3FU));
    output[2] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
    output[3] = (char)(0x80U | (codepoint & 0x3FU));
    return 4U;
}

char *duplicate_retro_vault_json_string(
    const struct retro_vault_json_value *value
)
{
    const char *cursor = NULL;
    const char *end = NULL;
    char *result = NULL;
    size_t output_size = 0U;

    if ((value == NULL) || (value->type != retro_vault_json_string) ||
        ((size_t)(value->end - value->start) < 2U)) {
        return NULL;
    }

    result = malloc((size_t)(value->end - value->start));
    if (result == NULL) {
        return NULL;
    }

    cursor = value->start + 1;
    end = value->end - 1;
    while (cursor < end) {
        unsigned char character = (unsigned char)*cursor++;

        if (character != '\\') {
            result[output_size++] = (char)character;
            continue;
        }

        if (cursor >= end) {
            free(result);
            return NULL;
        }

        character = (unsigned char)*cursor++;
        switch (character) {
        case '"': result[output_size++] = '"'; break;
        case '\\': result[output_size++] = '\\'; break;
        case '/': result[output_size++] = '/'; break;
        case 'b': result[output_size++] = '\b'; break;
        case 'f': result[output_size++] = '\f'; break;
        case 'n': result[output_size++] = '\n'; break;
        case 'r': result[output_size++] = '\r'; break;
        case 't': result[output_size++] = '\t'; break;
        case 'u': {
            uint32_t codepoint = 0U;
            if (((size_t)(end - cursor) < 4U) ||
                (decode_hex_quad(cursor, &codepoint) != 0)) {
                free(result);
                return NULL;
            }
            cursor += 4;

            if ((codepoint >= 0xD800U) && (codepoint <= 0xDBFFU) &&
                ((size_t)(end - cursor) >= 6U) &&
                (cursor[0] == '\\') && (cursor[1] == 'u')) {
                uint32_t low = 0U;
                if ((decode_hex_quad(cursor + 2, &low) == 0) &&
                    (low >= 0xDC00U) && (low <= 0xDFFFU)) {
                    codepoint = 0x10000U +
                        ((codepoint - 0xD800U) << 10U) +
                        (low - 0xDC00U);
                    cursor += 6;
                }
            }

            if ((codepoint >= 0xD800U) && (codepoint <= 0xDFFFU)) {
                codepoint = 0xFFFDU;
            }
            output_size += encode_utf8(codepoint, result + output_size);
            break;
        }
        default:
            free(result);
            return NULL;
        }
    }

    result[output_size] = '\0';
    return result;
}

int get_retro_vault_json_uint64(
    const struct retro_vault_json_value *value,
    uint64_t *number
)
{
    char text[32];
    char *end_pointer = NULL;
    size_t length = 0U;
    unsigned long long parsed = 0ULL;

    if ((value == NULL) || (number == NULL) ||
        (value->type != retro_vault_json_number)) {
        return -1;
    }

    length = (size_t)(value->end - value->start);
    if ((length == 0U) || (length >= sizeof(text)) ||
        (*value->start == '-')) {
        return -1;
    }

    memcpy(text, value->start, length);
    text[length] = '\0';
    errno = 0;
    parsed = strtoull(text, &end_pointer, 10);
    if ((errno != 0) || (end_pointer != text + length)) {
        return -1;
    }

    *number = (uint64_t)parsed;
    return 0;
}
