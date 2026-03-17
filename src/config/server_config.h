#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <stdbool.h>

#include "config/server_plugin_config.h"

enum server_run_mode {
    server_run_mode_console = 0,
    server_run_mode_daemon = 1
};

struct server_config {
    enum server_run_mode run_mode;
    bool show_help;
    char config_path[server_plugin_path_max];
};

int parse_server_config(
    struct server_config *config,
    int argc,
    char **argv,
    const char **error_message
);

const char *server_run_mode_name(enum server_run_mode run_mode);
void print_server_usage(const char *program_name);

#endif
