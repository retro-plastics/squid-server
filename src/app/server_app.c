/*
 * server_app.c
 *
 * Top-level application glue.  Parses command-line arguments into
 * a server_config, then hands off to the runtime to do the real
 * work.  Keeps main() trivial and testable.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#include "app/server_app.h"

#include "config/server_config.h"
#include "runtime/server_runtime.h"

#include <stdio.h>

int run_server_app(int argc, char **argv)
{
    const char *error_message = NULL;
    struct server_config config;
    struct server_runtime runtime;

    if (parse_server_config(&config, argc, argv, &error_message) != 0) {
        fprintf(stderr, "argument error: %s\n", error_message);
        print_server_usage(argv != NULL ? argv[0] : NULL);
        return 1;
    }

    if (config.show_help) {
        print_server_usage(argv != NULL ? argv[0] : NULL);
        return 0;
    }

    init_server_runtime(&runtime, &config);
    return run_server_runtime(&runtime);
}
