/*
 * server_log.c
 *
 * Logging back-end that writes to stdout (console mode) or to
 * the system logger via syslog(3) (daemon mode).  The active
 * back-end is chosen once by init_server_log() and is never
 * changed while the process is running.
 *
 * NOTES:
 *  In daemon mode the facility is LOG_DAEMON and the ident is
 *  "squid-server".  LOG_PID | LOG_NDELAY are used so that the
 *  PID appears in every log line and the socket is opened
 *  immediately (before the syslog daemon might restart).
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#include "log/server_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

static enum server_run_mode current_run_mode = server_run_mode_console;

static const char *server_log_level_name(enum server_log_level level)
{
    switch (level) {
    case server_log_level_info:
        return "info";
    case server_log_level_warning:
        return "warning";
    case server_log_level_error:
        return "error";
    default:
        return "unknown";
    }
}

static int server_log_level_priority(enum server_log_level level)
{
    switch (level) {
    case server_log_level_info:
        return LOG_INFO;
    case server_log_level_warning:
        return LOG_WARNING;
    case server_log_level_error:
        return LOG_ERR;
    default:
        return LOG_NOTICE;
    }
}

void init_server_log(enum server_run_mode run_mode)
{
    current_run_mode = run_mode;

    if (run_mode == server_run_mode_daemon) {
        openlog("squid-server", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
}

void close_server_log(void)
{
    if (current_run_mode == server_run_mode_daemon) {
        closelog();
    }
}

void write_server_log(enum server_log_level level, const char *message)
{
    const char *text = message != NULL ? message : "";

    if (current_run_mode == server_run_mode_daemon) {
        syslog(server_log_level_priority(level), "%s", text);
        return;
    }

    fprintf(stdout, "[%s] %s\n", server_log_level_name(level), text);
    fflush(stdout);
}

void write_server_log_errno(
    enum server_log_level level,
    const char *message,
    int error_number
)
{
    char buffer[512];
    const char *text = message != NULL ? message : "";
    const char *error_text = strerror(error_number);

    if ((error_text == NULL) || (error_text[0] == '\0')) {
        error_text = "unknown error";
    }

    snprintf(buffer, sizeof(buffer), "%s: %s", text, error_text);
    write_server_log(level, buffer);
}
