#include "runtime/server_runtime.h"

#include "log/server_log.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t keep_running = 1;

static void write_plugin_load_log(
    enum server_log_level level,
    uint8_t port_number,
    const char *plugin_path,
    const char *message
)
{
    char text[768];

    snprintf(
        text,
        sizeof(text),
        "plugin port %u path %s: %s",
        (unsigned int)port_number,
        plugin_path != NULL ? plugin_path : "(null)",
        message != NULL ? message : ""
    );
    write_server_log(level, text);
}

static void handle_termination_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static int install_signal_handlers(void)
{
    if (signal(SIGINT, handle_termination_signal) == SIG_ERR) {
        return -1;
    }

    if (signal(SIGTERM, handle_termination_signal) == SIG_ERR) {
        return -1;
    }

    return 0;
}

static int daemonize_process(void)
{
    pid_t process_id = 0;

    process_id = fork();
    if (process_id < 0) {
        return -1;
    }

    if (process_id > 0) {
        return 0;
    }

    if (setsid() < 0) {
        return -1;
    }

    process_id = fork();
    if (process_id < 0) {
        return -1;
    }

    if (process_id > 0) {
        _exit(0);
    }

    if (chdir("/") != 0) {
        return -1;
    }

    umask(0);

    if (freopen("/dev/null", "r", stdin) == NULL) {
        return -1;
    }

    if (freopen("/dev/null", "a", stdout) == NULL) {
        return -1;
    }

    if (freopen("/dev/null", "a", stderr) == NULL) {
        return -1;
    }

    return 1;
}

static void sleep_until_stopped(void)
{
    while (keep_running != 0) {
        sleep(1);
    }
}

void init_server_runtime(
    struct server_runtime *runtime,
    const struct server_config *config
)
{
    if ((runtime == NULL) || (config == NULL)) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->config = *config;
    init_server_plugin_config_file(&runtime->plugin_config);
    init_server_plugin_registry(&runtime->plugin_registry);
    init_server_plugin_loader(&runtime->plugin_loader);
}

static int load_configured_plugins(struct server_runtime *runtime)
{
    const char *config_error = NULL;
    char load_error[512];
    size_t index = 0;

    if (parse_server_plugin_config_file(
        &runtime->plugin_config,
        runtime->config.config_path,
        &config_error
    ) != 0) {
        write_server_log_errno(
            server_log_level_error,
            "failed to read plugin configuration",
            errno
        );
        if (config_error != NULL) {
            write_server_log(server_log_level_error, config_error);
        }
        return -1;
    }

    if (load_server_plugin(
        &runtime->plugin_loader,
        &runtime->plugin_registry,
        server_system_port,
        runtime->plugin_config.system_plugin_path,
        load_error,
        sizeof(load_error)
    ) != 0) {
        write_plugin_load_log(
            server_log_level_error,
            server_system_port,
            runtime->plugin_config.system_plugin_path,
            load_error
        );
        return -1;
    }

    write_plugin_load_log(
        server_log_level_info,
        server_system_port,
        runtime->plugin_config.system_plugin_path,
        "loaded"
    );

    for (index = 0; index < runtime->plugin_config.mapping_count; ++index) {
        const struct server_plugin_mapping *mapping =
            &runtime->plugin_config.mappings[index];

        if (load_server_plugin(
            &runtime->plugin_loader,
            &runtime->plugin_registry,
            mapping->port_number,
            mapping->plugin_path,
            load_error,
            sizeof(load_error)
        ) != 0) {
            write_plugin_load_log(
                server_log_level_error,
                mapping->port_number,
                mapping->plugin_path,
                load_error
            );
            return -1;
        }

        write_plugin_load_log(
            server_log_level_info,
            mapping->port_number,
            mapping->plugin_path,
            "loaded"
        );
    }

    return 0;
}

int run_server_runtime(struct server_runtime *runtime)
{
    int daemon_result = 0;
    char startup_message[128];

    if (runtime == NULL) {
        return 1;
    }

    if (runtime->config.run_mode == server_run_mode_daemon) {
        daemon_result = daemonize_process();
        if (daemon_result < 0) {
            write_server_log_errno(
                server_log_level_error,
                "failed to daemonize squid-server",
                errno
            );
            return 1;
        }

        if (daemon_result == 0) {
            return 0;
        }
    }

    init_server_log(runtime->config.run_mode);

    if (install_signal_handlers() != 0) {
        write_server_log_errno(
            server_log_level_error,
            "failed to install signal handlers",
            errno
        );
        close_server_log();
        return 1;
    }

    if (load_configured_plugins(runtime) != 0) {
        unload_server_plugins(&runtime->plugin_loader, &runtime->plugin_registry);
        close_server_log();
        return 1;
    }

    snprintf(
        startup_message,
        sizeof(startup_message),
        "squid-server starting in %s mode with %zu plugin slots and %zu loaded plugins",
        server_run_mode_name(runtime->config.run_mode),
        (size_t)server_port_count,
        runtime->plugin_loader.loaded_count
    );
    write_server_log(server_log_level_info, startup_message);

    sleep_until_stopped();

    write_server_log(server_log_level_info, "squid-server shutting down");
    unload_server_plugins(&runtime->plugin_loader, &runtime->plugin_registry);
    close_server_log();
    return 0;
}
