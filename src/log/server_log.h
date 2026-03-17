#ifndef SERVER_LOG_H
#define SERVER_LOG_H

#include "config/server_config.h"

enum server_log_level {
    server_log_level_info = 0,
    server_log_level_warning = 1,
    server_log_level_error = 2
};

void init_server_log(enum server_run_mode run_mode);
void close_server_log(void);
void write_server_log(enum server_log_level level, const char *message);
void write_server_log_errno(
    enum server_log_level level,
    const char *message,
    int error_number
);

#endif
