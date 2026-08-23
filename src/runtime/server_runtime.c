/*
 * server_runtime.c
 *
 * Core server loop.  Handles daemonisation, signal setup, plugin
 * loading, Unix-socket transport initialisation, and the squid
 * protocol dispatch loop.  Each call to snet_burst() drives the
 * libsquid engine; received packets are dispatched to the plugin
 * registered on the matching wire channel and responses are
 * queued for the next TX burst.
 *
 * NOTES:
 *  _XOPEN_SOURCE 600 is required for usleep().
 *  The dispatch loop exits when snet_link_is_up() returns false,
 *  which happens on peer-restart or after max retransmit
 *  attempts.  WAITING state (data in-flight) keeps link_up set,
 *  so the loop continues through normal data transfer.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#define _XOPEN_SOURCE 600

#include "runtime/server_runtime.h"

#include "transport/local/local_transport.h"
#include "transport/serial/serial_transport.h"

#include "log/server_log.h"

#include <squid/snet.h>
#include <squid/socket.h>

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * libsquid timing parameters (values in ticks; one tick = 20 ms).
 *   timeout_ticks 200  →    4 s resend timeout (safe for slow clients and
 *                                debug emulation)
 *   ack_delay_ticks  1 →   20 ms for a response to piggyback its ACK;
 *                                avoids back-to-back frames overrunning a
 *                                polling Partner at 9600 baud
 *   ping_ticks      0  →  disabled while plugins may block on I/O
 *   max_retries     5
 */
static const squid_timing_t squid_timing = {
    .timeout_ticks = 200U,
    .ack_delay_ticks = 1U,
    .ping_ticks = 0U,
    .max_retries = 5U
};

static volatile sig_atomic_t keep_running = 1;

/* ---- helpers ---- */

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

/* ---- signal handling ---- */

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

/* ---- daemonization ---- */

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

/* ---- plugin loading ---- */

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

/* ---- libsquid socket management ---- */

static void open_plugin_sockets(struct server_runtime *runtime)
{
    uint8_t port = 0;

    for (port = server_plugin_port_min; port <= server_plugin_port_max; ++port) {
        if (find_server_plugin(&runtime->plugin_registry, port) == NULL) {
            continue;
        }

        runtime->squid_fds[port] = squid_open(
            runtime->squid_tx_rings[port],
            (uint8_t)server_squid_ring_size,
            runtime->squid_rx_rings[port],
            (uint8_t)server_squid_ring_size
        );
        if (runtime->squid_fds[port] < 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                "squid_open failed for port %u — plugin will not receive packets",
                (unsigned int)port);
            write_server_log(server_log_level_warning, msg);
            continue;
        }

        /* Wire channel N is bound directly to server port N. */
        if (squid_bind(runtime->squid_fds[port], port) != 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                "squid_bind failed for port %u — plugin will not receive packets",
                (unsigned int)port);
            write_server_log(server_log_level_warning, msg);
            squid_close(runtime->squid_fds[port]);
            runtime->squid_fds[port] = -1;
        }
    }
}

static void close_plugin_sockets(struct server_runtime *runtime)
{
    uint8_t port = 0;

    for (port = server_plugin_port_min; port <= server_plugin_port_max; ++port) {
        if (runtime->squid_fds[port] >= 0) {
            squid_close(runtime->squid_fds[port]);
            runtime->squid_fds[port] = -1;
        }
    }
}

/* ---- packet dispatch ---- */

/*
 * libsquid channels are byte streams.  The server's plugin API is packet
 * based, so each stream message is prefixed by its one-byte payload length.
 * Read only the bytes needed for one packet and leave a following packet in
 * the socket ring for the next dispatch pass.
 */
static int receive_plugin_packet(
    struct server_runtime *runtime,
    uint8_t port
)
{
    size_t *size = &runtime->request_packet_sizes[port];
    size_t *expected = &runtime->request_packet_expected[port];
    int received = 0;

    if (*expected == 0U) {
        uint8_t wire_size = 0U;

        received = squid_recv(runtime->squid_fds[port], &wire_size, 1U);
        if (received <= 0) {
            return received;
        }
        if ((wire_size == 0U) || (wire_size > server_packet_max)) {
            write_server_log(server_log_level_warning,
                "invalid zero-length or oversized squid packet");
            return -1;
        }
        *expected = wire_size;
        *size = 0U;
    }

    if (*size < *expected) {
        received = squid_recv(
            runtime->squid_fds[port],
            runtime->request_packets[port] + *size,
            (uint16_t)(*expected - *size)
        );
        if (received < 0) {
            return -1;
        }
        *size += (size_t)received;
    }

    return *size == *expected ? 1 : 0;
}

static void dispatch_received_packets(struct server_runtime *runtime)
{
    uint8_t port = 0;

    for (port = server_plugin_port_min; port <= server_plugin_port_max; ++port) {
        const struct server_plugin *plugin = NULL;
        int packet_ready = 0;

        if (runtime->squid_fds[port] < 0) {
            continue;
        }

        /*
         * libsquid applies all-or-nothing TX backpressure.  Keep a
         * plugin response until the socket ring has room rather than losing
         * it, and stop draining this port's RX ring while a reply is pending.
         */
        if (runtime->pending_response_sizes[port] > 0U) {
            if (squid_send(
                runtime->squid_fds[port],
                runtime->pending_responses[port],
                (uint16_t)runtime->pending_response_sizes[port]
            ) < 0) {
                continue;
            }

            runtime->pending_response_sizes[port] = 0U;
        }

        packet_ready = receive_plugin_packet(runtime, port);
        if (packet_ready <= 0) {
            continue;
        }

        plugin = find_server_plugin(&runtime->plugin_registry, port);
        if ((plugin == NULL) || (plugin->handle_packet == NULL)) {
            continue;
        }

        {
            struct server_packet_view request;
            struct server_packet_buffer response;

            request.packet_data = runtime->request_packets[port];
            request.packet_size = runtime->request_packet_sizes[port];

            response.packet_data = runtime->pending_responses[port] + 1U;
            response.packet_capacity = server_packet_max;
            response.packet_size     = 0U;

            if (plugin->handle_packet(
                (struct server_plugin *)plugin,
                port,
                &request,
                &response
            ) == 0) {
                if ((response.packet_size > 0U) &&
                    ((response.packet_data != runtime->pending_responses[port] + 1U) ||
                     (response.packet_size > server_packet_max))) {
                    char msg[192];
                    snprintf(
                        msg,
                        sizeof(msg),
                        "plugin %s on port %u returned an invalid response — discarded",
                        plugin->plugin_name != NULL ? plugin->plugin_name : "(unnamed)",
                        (unsigned int)port
                    );
                    write_server_log(server_log_level_warning, msg);
                } else if (response.packet_size > 0U) {
                    size_t framed_size = response.packet_size + 1U;

                    runtime->pending_responses[port][0] =
                        (uint8_t)response.packet_size;
                    if (squid_send(
                        runtime->squid_fds[port],
                        runtime->pending_responses[port],
                        (uint16_t)framed_size
                    ) >= 0) {
                        framed_size = 0U;
                    }

                    runtime->pending_response_sizes[port] = framed_size;
                }
            }

            runtime->request_packet_sizes[port] = 0U;
            runtime->request_packet_expected[port] = 0U;
        }
    }
}

/* ---- main dispatch loop ---- */

static void run_dispatch_loop(struct server_runtime *runtime)
{
    while (keep_running) {
        /* Wait for the first client or a replacement client's handshake. */
        while (keep_running && !snet_link_is_up()) {
            snet_burst();
            usleep(5000);
        }

        if (!keep_running) {
            return;
        }

        write_server_log(server_log_level_info, "squid link established");

        while (keep_running && snet_link_is_up()) {
            snet_burst();
            dispatch_received_packets(runtime);
            usleep(5000);   /* 5 ms — well within one 20 ms tick period */
        }

        if (!snet_link_is_up()) {
            uint8_t port = 0U;

            write_server_log(server_log_level_info, "squid link lost; waiting for a new handshake");
            for (port = server_plugin_port_min;
                 port <= server_plugin_port_max;
                 ++port) {
                runtime->request_packet_sizes[port] = 0U;
                runtime->request_packet_expected[port] = 0U;
                runtime->pending_response_sizes[port] = 0U;
            }
        }
    }
}

/* ---- public API ---- */

void init_server_runtime(
    struct server_runtime *runtime,
    const struct server_config *config
)
{
    int i = 0;

    if ((runtime == NULL) || (config == NULL)) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->config = *config;
    init_server_plugin_config_file(&runtime->plugin_config);
    init_server_plugin_registry(&runtime->plugin_registry);
    init_server_plugin_loader(&runtime->plugin_loader);

    for (i = 0; i < server_port_count; ++i) {
        runtime->squid_fds[i] = -1;
    }
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
            return 0;   /* original parent exits cleanly */
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

    /* Set up transport: serial when serial_device is configured, local otherwise. */
    if (runtime->plugin_config.serial_device[0] != '\0') {
        char open_msg[128];

        snprintf(open_msg, sizeof(open_msg),
            "opening serial transport on %s at %d baud",
            runtime->plugin_config.serial_device,
            runtime->plugin_config.serial_baud);
        write_server_log(server_log_level_info, open_msg);

        {
            struct serial_transport_config serial_cfg;
            serial_cfg.device   = runtime->plugin_config.serial_device;
            serial_cfg.baud     = runtime->plugin_config.serial_baud;
            serial_cfg.databits = runtime->plugin_config.serial_databits;
            serial_cfg.parity   = runtime->plugin_config.serial_parity;
            serial_cfg.stopbits = runtime->plugin_config.serial_stopbits;
            serial_cfg.flow     = runtime->plugin_config.serial_flow;

            if (serial_transport_open(
                &runtime->serial_transport,
                &serial_cfg
            ) != 0) {
                write_server_log_errno(
                    server_log_level_error,
                    "failed to open serial transport",
                    errno
                );
                unload_server_plugins(&runtime->plugin_loader, &runtime->plugin_registry);
                close_server_log();
                return 1;
            }
        }

        serial_transport_activate(&runtime->serial_transport);
        snet_init(&runtime->serial_transport.base.platform, &squid_timing);
    } else {
        if (local_transport_listen(
            &runtime->transport,
            local_transport_default_socket_path
        ) != 0) {
            write_server_log_errno(
                server_log_level_error,
                "failed to create local transport socket",
                errno
            );
            unload_server_plugins(&runtime->plugin_loader, &runtime->plugin_registry);
            close_server_log();
            return 1;
        }

        {
            char listen_msg[128];
            snprintf(listen_msg, sizeof(listen_msg),
                "waiting for client on %s", local_transport_default_socket_path);
            write_server_log(server_log_level_info, listen_msg);
        }

        if (local_transport_accept(&runtime->transport) != 0) {
            write_server_log_errno(
                server_log_level_error,
                "failed to accept client connection",
                errno
            );
            local_transport_close(&runtime->transport);
            unload_server_plugins(&runtime->plugin_loader, &runtime->plugin_registry);
            close_server_log();
            return 1;
        }

        write_server_log(server_log_level_info, "client connected");

        local_transport_activate(&runtime->transport);
        snet_init(&runtime->transport.base.platform, &squid_timing);
    }

    /* Open a squid socket for each plugin registered on ports 1-15. */
    open_plugin_sockets(runtime);

    /* Run the packet dispatch loop. */
    run_dispatch_loop(runtime);

    /* Tear down. */
    close_plugin_sockets(runtime);
    if (runtime->plugin_config.serial_device[0] != '\0') {
        serial_transport_close(&runtime->serial_transport);
    } else {
        local_transport_close(&runtime->transport);
    }

    write_server_log(server_log_level_info, "squid-server shutting down");
    unload_server_plugins(&runtime->plugin_loader, &runtime->plugin_registry);
    close_server_log();
    return 0;
}
