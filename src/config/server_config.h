/*
 * server_config.h
 *
 * Run-mode enumeration and server_config struct used throughout
 * the server.  Also exposes the command-line parser and helpers
 * for printing the usage banner.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <stdbool.h>

#include "config/server_plugin_config.h"

/* How the server process runs — foreground or background. */
enum server_run_mode {
    server_run_mode_console = 0, /* log to stdout, stay in foreground */
    server_run_mode_daemon  = 1  /* double-fork, redirect to /dev/null */
};

/* Parsed result of the command-line arguments. */
struct server_config {
    enum server_run_mode run_mode;
    bool show_help;
    char config_path[server_plugin_path_max];
};

/*
 * Populate *config from argc/argv.
 * On failure, *error_message points to a static string.
 * Returns 0 on success, -1 on error.
 */
int parse_server_config(
    struct server_config *config,
    int argc,
    char **argv,
    const char **error_message
);

/* Return a human-readable name for the given run mode. */
const char *server_run_mode_name(enum server_run_mode run_mode);

/* Print a one-screen usage banner to stdout. */
void print_server_usage(const char *program_name);

#endif
