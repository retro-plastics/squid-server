/* System-identification plugin client. */
#ifndef SQUID_CLIENT_SYSTEM_H
#define SQUID_CLIENT_SYSTEM_H

#include "squid_client/base.h"

#ifdef __cplusplus
extern "C" {
#endif

int squid_client_system_id(
    squid_client_t *client,
    squid_client_bytes_t *reply
);

#ifdef __cplusplus
}
#endif

#endif
