#include "config/server_plugin_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim_left(char *text)
{
    while ((*text != '\0') && isspace((unsigned char)*text)) {
        ++text;
    }

    return text;
}

static void trim_right(char *text)
{
    size_t length = strlen(text);

    while (length > 0U) {
        if (!isspace((unsigned char)text[length - 1U])) {
            break;
        }

        text[length - 1U] = '\0';
        --length;
    }
}

static int copy_plugin_path(char *destination, const char *source)
{
    size_t length = 0;

    if ((destination == NULL) || (source == NULL)) {
        return -1;
    }

    length = strlen(source);
    if (length >= server_plugin_path_max) {
        return -1;
    }

    memcpy(destination, source, length + 1U);
    return 0;
}

static struct server_plugin_mapping *find_plugin_mapping(
    struct server_plugin_config_file *config_file,
    uint8_t port_number
)
{
    size_t index = 0;

    for (index = 0; index < config_file->mapping_count; ++index) {
        if (config_file->mappings[index].port_number == port_number) {
            return &config_file->mappings[index];
        }
    }

    return NULL;
}

static int parse_port_number(const char *text, uint8_t *port_number)
{
    char *tail = NULL;
    long parsed_value = 0;

    if ((text == NULL) || (port_number == NULL)) {
        return -1;
    }

    parsed_value = strtol(text, &tail, 10);
    if ((tail == text) || (tail == NULL) || (*tail != '\0')) {
        return -1;
    }

    if ((parsed_value < 0L) || (parsed_value > 255L)) {
        return -1;
    }

    *port_number = (uint8_t)parsed_value;
    return 0;
}

static int parse_system_plugin_line(
    struct server_plugin_config_file *config_file,
    char *path_token,
    char *extra_token,
    const char **error_message
)
{
    if ((path_token == NULL) || (extra_token != NULL)) {
        *error_message = "invalid system_plugin directive";
        return -1;
    }

    if (copy_plugin_path(config_file->system_plugin_path, path_token) != 0) {
        *error_message = "system plugin path is too long";
        return -1;
    }

    return 0;
}

static int parse_plugin_line(
    struct server_plugin_config_file *config_file,
    char *port_token,
    char *path_token,
    char *extra_token,
    const char **error_message
)
{
    struct server_plugin_mapping *mapping = NULL;
    uint8_t port_number = 0;

    if ((port_token == NULL) || (path_token == NULL) || (extra_token != NULL)) {
        *error_message = "invalid plugin directive";
        return -1;
    }

    if (parse_port_number(port_token, &port_number) != 0) {
        *error_message = "invalid port number";
        return -1;
    }

    if (!is_valid_assignable_server_port(port_number)) {
        *error_message = "plugin ports must be in the range 1-15";
        return -1;
    }

    mapping = find_plugin_mapping(config_file, port_number);
    if (mapping != NULL) {
        *error_message = "duplicate plugin port assignment";
        return -1;
    }

    if (config_file->mapping_count >= server_plugin_mapping_max) {
        *error_message = "too many plugin mappings";
        return -1;
    }

    mapping = &config_file->mappings[config_file->mapping_count];
    mapping->port_number = port_number;

    if (copy_plugin_path(mapping->plugin_path, path_token) != 0) {
        *error_message = "plugin path is too long";
        return -1;
    }

    ++config_file->mapping_count;
    return 0;
}

void init_server_plugin_config_file(struct server_plugin_config_file *config_file)
{
    if (config_file == NULL) {
        return;
    }

    memset(config_file, 0, sizeof(*config_file));
    copy_plugin_path(
        config_file->system_plugin_path,
        server_default_system_plugin_path
    );
}

int parse_server_plugin_config_file(
    struct server_plugin_config_file *config_file,
    const char *file_path,
    const char **error_message
)
{
    FILE *input = NULL;
    char line_buffer[1024];
    unsigned int line_number = 0;

    if (config_file == NULL) {
        if (error_message != NULL) {
            *error_message = "missing plugin configuration output";
        }
        return -1;
    }

    init_server_plugin_config_file(config_file);

    if (error_message != NULL) {
        *error_message = NULL;
    }

    if (file_path == NULL) {
        if (error_message != NULL) {
            *error_message = "missing plugin configuration path";
        }
        return -1;
    }

    input = fopen(file_path, "r");
    if (input == NULL) {
        if (error_message != NULL) {
            *error_message = "failed to open plugin configuration";
        }
        return -1;
    }

    while (fgets(line_buffer, sizeof(line_buffer), input) != NULL) {
        char *line = NULL;
        char *keyword = NULL;
        char *first_value = NULL;
        char *second_value = NULL;
        char *extra_value = NULL;
        const char *line_error = NULL;

        ++line_number;

        trim_right(line_buffer);
        line = trim_left(line_buffer);

        if ((*line == '\0') || (*line == '#')) {
            continue;
        }

        keyword = strtok(line, " \t");
        first_value = strtok(NULL, " \t");
        second_value = strtok(NULL, " \t");
        extra_value = strtok(NULL, " \t");

        if ((keyword != NULL) && (strcmp(keyword, "system_plugin") == 0)) {
            if (parse_system_plugin_line(
                config_file,
                first_value,
                second_value,
                &line_error
            ) != 0) {
                if (error_message != NULL) {
                    *error_message = line_error;
                }
                fclose(input);
                return -1;
            }
            continue;
        }

        if ((keyword != NULL) && (strcmp(keyword, "plugin") == 0)) {
            if (parse_plugin_line(
                config_file,
                first_value,
                second_value,
                extra_value,
                &line_error
            ) != 0) {
                if (error_message != NULL) {
                    *error_message = line_error;
                }
                fclose(input);
                return -1;
            }
            continue;
        }

        if (error_message != NULL) {
            *error_message = "unknown plugin configuration directive";
        }
        fclose(input);
        return -1;
    }

    fclose(input);
    return 0;
}
