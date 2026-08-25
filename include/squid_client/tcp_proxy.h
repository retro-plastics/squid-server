/* Raw TCP proxy plugin client. */
#ifndef SQUID_CLIENT_TCP_PROXY_H
#define SQUID_CLIENT_TCP_PROXY_H

#include "squid_client/base.h"
#include "squid_server/tcp_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct squid_client_tcp_read {
    uint8_t eof;
    squid_client_bytes_t data;
} squid_client_tcp_read_t;

int squid_client_tcp_connect(
    squid_client_t *client,
    const char *host,
    uint16_t port,
    uint8_t *family
);
int squid_client_tcp_write(
    squid_client_t *client,
    const void *data,
    uint8_t size,
    uint8_t *written
);
int squid_client_tcp_read(
    squid_client_t *client,
    uint16_t maximum_wait_ms,
    uint8_t maximum_bytes,
    squid_client_tcp_read_t *chunk
);
int squid_client_tcp_close(squid_client_t *client);
int squid_client_tcp_status(squid_client_t *client, uint8_t *connected);

#ifdef __cplusplus
}
#endif

#endif
