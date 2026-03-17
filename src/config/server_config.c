/*
 * server_config.c
 *
 * Command-line argument parser for squid-server.  Recognises
 * --console, --daemon, --config <path>, and --help.  Produces a
 * populated server_config struct; errors are returned as a
 * caller-owned string pointer so the caller can format the
 * usage message.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#include "config/server_config.h"

#include <stdio.h>
#include <string.h>

static void init_server_config(struct server_config *config)
{
    config->run_mode = server_run_mode_console;
    config->show_help = false;
    memcpy(
        config->config_path,
        server_default_config_path,
        sizeof(server_default_config_path)
    );
}

int parse_server_config(
    struct server_config *config,
    int argc,
    char **argv,
    const char **error_message
)
{
    int index = 0;

    if (config == NULL) {
        if (error_message != NULL) {
            *error_message = "missing configuration output";
        }
        return -1;
    }

    init_server_config(config);

    if (error_message != NULL) {
        *error_message = NULL;
    }

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if ((strcmp(argument, "--help") == 0) || (strcmp(argument, "-h") == 0)) {
            config->show_help = true;
            return 0;
        }

        if (strcmp(argument, "--console") == 0) {
            config->run_mode = server_run_mode_console;
            continue;
        }

        if (strcmp(argument, "--daemon") == 0) {
            config->run_mode = server_run_mode_daemon;
            continue;
        }

        if (strcmp(argument, "--config") == 0) {
            size_t path_length = 0;

            ++index;
            if (index >= argc) {
                if (error_message != NULL) {
                    *error_message = "missing path after --config";
                }
                return -1;
            }

            path_length = strlen(argv[index]);
            if (path_length >= server_plugin_path_max) {
                if (error_message != NULL) {
                    *error_message = "configuration path is too long";
                }
                return -1;
            }

            memcpy(config->config_path, argv[index], path_length + 1U);
            continue;
        }

        if (error_message != NULL) {
            *error_message = "unknown argument";
        }
        return -1;
    }

    return 0;
}

const char *server_run_mode_name(enum server_run_mode run_mode)
{
    switch (run_mode) {
    case server_run_mode_console:
        return "console";
    case server_run_mode_daemon:
        return "daemon";
    default:
        return "unknown";
    }
}

void print_server_usage(const char *program_name)
{
    const char *name = program_name != NULL ? program_name : "squid-server";

    printf("usage: %s [--console | --daemon] [--config path] [--help]\n", name);
    printf("  --console  run in the foreground\n");
    printf("  --daemon   detach and run as a classic linux daemon\n");
    printf("  --config   read plugin mapping configuration from a file\n");
    printf("  --help     show this help message\n");
}
