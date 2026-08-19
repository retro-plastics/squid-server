#ifndef SQUID_TCP_PROXY_CORE_H
#define SQUID_TCP_PROXY_CORE_H

#include <stddef.h>
#include <stdint.h>

#define squid_tcp_allowlist_max 1024U

struct squid_tcp_context {
    int socket_fd;
    unsigned int connect_timeout_ms;
    unsigned int write_timeout_ms;
    char allowed_hosts[squid_tcp_allowlist_max];
};

void init_squid_tcp_context(
    struct squid_tcp_context *context,
    const char *allowed_hosts,
    unsigned int connect_timeout_ms,
    unsigned int write_timeout_ms
);

void free_squid_tcp_context(struct squid_tcp_context *context);

int handle_squid_tcp_request(
    struct squid_tcp_context *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
);

#endif
