#define _XOPEN_SOURCE 700

#include "config/server_config.h"
#include "config/server_plugin_config.h"
#include "plugin/server_plugin_registry.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_parse_defaults(void)
{
    char *argv[] = { "squid-server" };
    struct server_config config;
    const char *error_message = NULL;

    if (parse_server_config(&config, 1, argv, &error_message) != 0) {
        return 1;
    }

    if (config.run_mode != server_run_mode_console) {
        return 1;
    }

    if (config.show_help) {
        return 1;
    }

    return strcmp(config.config_path, server_default_config_path) == 0 ? 0 : 1;
}

static int test_parse_config_flag(void)
{
    char *argv[] = { "squid-server", "--config", "/tmp/test.conf" };
    struct server_config config;
    const char *error_message = NULL;

    if (parse_server_config(&config, 3, argv, &error_message) != 0) {
        return 1;
    }

    return strcmp(config.config_path, "/tmp/test.conf") == 0 ? 0 : 1;
}

static int test_parse_daemon_flag(void)
{
    char *argv[] = { "squid-server", "--daemon" };
    struct server_config config;
    const char *error_message = NULL;

    if (parse_server_config(&config, 2, argv, &error_message) != 0) {
        return 1;
    }

    return config.run_mode == server_run_mode_daemon ? 0 : 1;
}

static int test_parse_unknown_flag(void)
{
    char *argv[] = { "squid-server", "--mystery" };
    struct server_config config;
    const char *error_message = NULL;

    if (parse_server_config(&config, 2, argv, &error_message) == 0) {
        return 1;
    }

    if (error_message == NULL) {
        return 1;
    }

    return strcmp(error_message, "unknown argument") == 0 ? 0 : 1;
}

static int test_plugin_registry(void)
{
    struct server_plugin_registry registry;
    static const struct server_plugin plugin = { 0 };

    init_server_plugin_registry(&registry);

    if (count_registered_server_plugins(&registry) != 0) {
        return 1;
    }

    if (register_server_plugin(&registry, 0, &plugin) != 0) {
        return 1;
    }

    if (register_server_plugin(&registry, 1, &plugin) != 0) {
        return 1;
    }

    if (register_server_plugin(&registry, 1, &plugin) == 0) {
        return 1;
    }

    if (find_server_plugin(&registry, 1) != &plugin) {
        return 1;
    }

    if (find_server_plugin(&registry, 16) != NULL) {
        return 1;
    }

    if (count_registered_server_plugins(&registry) != 2) {
        return 1;
    }

    return 0;
}

static int test_plugin_config_file(void)
{
    static const char config_text[] =
        "# comment\n"
        "system_plugin ./bin/plugins/libsquidsys.so\n"
        "plugin 1 ./bin/plugins/libecho.so\n";
    char file_path[] = "/tmp/squid_server_test_XXXXXX";
    int file_descriptor = -1;
    struct server_plugin_config_file config_file;
    const char *error_message = NULL;
    ssize_t bytes_written = 0;
    int result = 1;

    file_descriptor = mkstemp(file_path);
    if (file_descriptor < 0) {
        return 1;
    }

    bytes_written = write(
        file_descriptor,
        config_text,
        (size_t)(sizeof(config_text) - 1U)
    );
    if (bytes_written != (ssize_t)(sizeof(config_text) - 1U)) {
        close(file_descriptor);
        unlink(file_path);
        return 1;
    }

    close(file_descriptor);

    if (parse_server_plugin_config_file(&config_file, file_path, &error_message) != 0) {
        unlink(file_path);
        return 1;
    }

    if (strcmp(
        config_file.system_plugin_path,
        "./bin/plugins/libsquidsys.so"
    ) != 0) {
        unlink(file_path);
        return 1;
    }

    if (config_file.mapping_count != 1U) {
        unlink(file_path);
        return 1;
    }

    if (config_file.mappings[0].port_number != 1U) {
        unlink(file_path);
        return 1;
    }

    if (strcmp(config_file.mappings[0].plugin_path, "./bin/plugins/libecho.so") != 0) {
        unlink(file_path);
        return 1;
    }

    result = 0;
    unlink(file_path);
    return result;
}

int main(void)
{
    if (test_parse_defaults() != 0) {
        fprintf(stderr, "test_parse_defaults failed\n");
        return 1;
    }

    if (test_parse_daemon_flag() != 0) {
        fprintf(stderr, "test_parse_daemon_flag failed\n");
        return 1;
    }

    if (test_parse_config_flag() != 0) {
        fprintf(stderr, "test_parse_config_flag failed\n");
        return 1;
    }

    if (test_parse_unknown_flag() != 0) {
        fprintf(stderr, "test_parse_unknown_flag failed\n");
        return 1;
    }

    if (test_plugin_registry() != 0) {
        fprintf(stderr, "test_plugin_registry failed\n");
        return 1;
    }

    if (test_plugin_config_file() != 0) {
        fprintf(stderr, "test_plugin_config_file failed\n");
        return 1;
    }

    puts("all tests passed");
    return 0;
}
