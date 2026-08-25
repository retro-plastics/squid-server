#include "squid_client/spectrum_if1.h"

#include <stdint.h>

int main(void)
{
    struct squid_client client;
    uint8_t workspace[17];

    return squid_client_spectrum_if1_open(
        &client, 5U, workspace, 16U) == SQUID_CLIENT_ERROR_ARGUMENT;
}
