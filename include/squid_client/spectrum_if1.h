/* ZX Spectrum 48K + Interface 1, 115200-8-N-2 client transport. */
#ifndef SQUID_CLIENT_SPECTRUM_IF1_H
#define SQUID_CLIENT_SPECTRUM_IF1_H

#include "squid_client/base.h"

#include <squid/snet.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SQUID_CLIENT_SPECTRUM_IF1_BAUD      115200UL
#define SQUID_CLIENT_SPECTRUM_IF1_STOP_BITS 2U

/* Fill a libsquid platform table with the Interface 1 bit-banged hooks. */
void squid_client_spectrum_if1_platform(squid_platform_t *platform);

/* Reset the receiver and leave Interface 1 flow control in the stopped state. */
void squid_client_spectrum_if1_reset(void);

/* Initialize libsquid, allocate queues, connect a channel, and initialize the
 * client. A 50 Hz interrupt must call snet_burst(); typed calls can then block
 * synchronously without an idle callback. Returns zero or a negative
 * SQUID_CLIENT_ERROR_* value. */
int squid_client_spectrum_if1_open(
    struct squid_client *client,
    uint8_t channel,
    uint8_t *workspace,
    uint16_t packet_capacity
);

#ifdef __cplusplus
}
#endif

#endif
