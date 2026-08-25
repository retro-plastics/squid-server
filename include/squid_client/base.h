/* Small synchronous client core shared by every plugin wrapper. */
#ifndef SQUID_CLIENT_BASE_H
#define SQUID_CLIENT_BASE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SQUID_CLIENT_PACKET_MAX 255U
#define SQUID_CLIENT_WORKSPACE_SIZE (SQUID_CLIENT_PACKET_MAX + 1U)

#define SQUID_CLIENT_ERROR_ARGUMENT  -1
#define SQUID_CLIENT_ERROR_LINK      -2
#define SQUID_CLIENT_ERROR_IO        -3
#define SQUID_CLIENT_ERROR_PROTOCOL  -4
#define SQUID_CLIENT_ERROR_OVERFLOW  -5
#define SQUID_CLIENT_ERROR_CANCELLED -6

/* Called while a synchronous request is waiting. Return non-zero to cancel.
 * Interrupt-driven Z80 programs normally pass NULL. */
typedef int (*squid_client_idle_fn)(void *user_data);

typedef struct squid_client {
    int socket_fd;
    uint8_t *packet;
    uint16_t packet_capacity;
    squid_client_idle_fn idle;
    void *idle_context;
} squid_client_t;

/* A zero-copy result. It remains valid until the next call on the client. */
typedef struct squid_client_bytes {
    const uint8_t *data;
    uint8_t size;
} squid_client_bytes_t;

/* workspace must hold packet_capacity + 1 bytes. Socket TX and RX queues
 * should each hold at least the same amount. */
void squid_client_init(
    squid_client_t *client,
    int socket_fd,
    uint8_t *workspace,
    uint16_t packet_capacity,
    squid_client_idle_fn idle,
    void *idle_context
);

/* Low-level escape hatch. Put request bytes at client->packet + 1. On success
 * the response starts at client->packet and its byte count is returned. */
int squid_client_exchange(squid_client_t *client, uint16_t request_size);

#ifdef __cplusplus
}
#endif

#endif
