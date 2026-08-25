/* Echo plugin client. */
#ifndef SQUID_CLIENT_ECHO_H
#define SQUID_CLIENT_ECHO_H

#include "squid_client/base.h"

#ifdef __cplusplus
extern "C" {
#endif

int squid_client_echo(
    squid_client_t *client,
    const void *data,
    uint8_t size,
    squid_client_bytes_t *reply
);

#ifdef __cplusplus
}
#endif

#endif
