#ifndef RETROVAULT_JSON_H
#define RETROVAULT_JSON_H

#include <stddef.h>
#include <stdint.h>

enum retro_vault_json_type {
    retro_vault_json_invalid = 0,
    retro_vault_json_object,
    retro_vault_json_array,
    retro_vault_json_string,
    retro_vault_json_number,
    retro_vault_json_boolean,
    retro_vault_json_null
};

struct retro_vault_json_value {
    const char *start;
    const char *end;
    enum retro_vault_json_type type;
};

struct retro_vault_json_iterator {
    const char *cursor;
    const char *end;
    int finished;
};

int parse_retro_vault_json(
    const char *data,
    size_t size,
    struct retro_vault_json_value *value
);

int get_retro_vault_json_member(
    const struct retro_vault_json_value *object,
    const char *name,
    struct retro_vault_json_value *value
);

int init_retro_vault_json_array_iterator(
    const struct retro_vault_json_value *array,
    struct retro_vault_json_iterator *iterator
);

int next_retro_vault_json_array_value(
    struct retro_vault_json_iterator *iterator,
    struct retro_vault_json_value *value
);

char *duplicate_retro_vault_json_string(
    const struct retro_vault_json_value *value
);

int get_retro_vault_json_uint64(
    const struct retro_vault_json_value *value,
    uint64_t *number
);

#endif
