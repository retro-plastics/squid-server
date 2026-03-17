/*
 * server_log.h
 *
 * Thin logging interface that abstracts console vs. syslog
 * output.  Call init_server_log() once at startup and
 * close_server_log() at shutdown.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_LOG_H
#define SERVER_LOG_H

#include "config/server_config.h"

/* Severity levels, ordered from least to most severe. */
enum server_log_level {
    server_log_level_info    = 0,
    server_log_level_warning = 1,
    server_log_level_error   = 2
};

/* Select the back-end (console or syslog) for this run. */
void init_server_log(enum server_run_mode run_mode);

/* Flush and close the syslog connection (no-op for console). */
void close_server_log(void);

/* Emit a plain text log entry at the given severity level. */
void write_server_log(
    enum server_log_level level,
    const char *message
);

/*
 * Emit a log entry that appends ": <strerror(error_number)>"
 * to message, useful for syscall failures.
 */
void write_server_log_errno(
    enum server_log_level level,
    const char *message,
    int error_number
);

#endif
