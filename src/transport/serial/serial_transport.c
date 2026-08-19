/*
 * serial_transport.c
 *
 * POSIX serial port (TTY) transport implementation.
 * Provides the three libsquid platform hooks — send_char,
 * recv_char, and get_tick — wired to an open serial fd configured
 * for raw non-blocking I/O.
 *
 * Line parameters (baud, data bits, parity, stop bits, flow
 * control) are taken from a serial_transport_config struct so
 * that retro hardware combinations such as 7E2 + RTS/CTS are
 * fully supported.
 *
 * recv_char returns -1 immediately when no byte is available
 * (O_NONBLOCK), so snet_burst() never blocks.  get_tick returns
 * a wrapping 8-bit counter in 20 ms units derived from
 * CLOCK_MONOTONIC (same as the local transport).
 *
 * NOTES:
 *  A module-level static int transport_fd holds the active fd
 *  because the libsquid platform_t hooks have no user-data
 *  pointer.  This restricts the process to one active transport
 *  at a time, which is sufficient for the current server model.
 *  _POSIX_C_SOURCE 200112L is required for clock_gettime().
 *  _DEFAULT_SOURCE is required for CRTSCTS on Linux.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include "transport/serial/serial_transport.h"

#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * File-scope fd used by the libsquid platform hooks.
 * Set by serial_transport_activate() before snet_init() is called.
 */
static int transport_fd = -1;

/* ---- baud rate mapping ---- */

static speed_t map_baud_rate(int baud)
{
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

/* ---- libsquid platform hook implementations ---- */

static int serial_send_char(uint8_t c)
{
    return (write(transport_fd, &c, 1) == 1) ? 0 : -1;
}

static int serial_recv_char(void)
{
    uint8_t c;
    ssize_t n = read(transport_fd, &c, 1);
    return (n == 1) ? (int)(unsigned int)c : -1;
}

static uint8_t serial_get_tick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /*
     * 20 ms ticks (50 per second).  The result is cast to uint8_t so it
     * wraps naturally, which is what libsquid expects for timeout arithmetic.
     */
    return (uint8_t)((ts.tv_sec * 50UL + (unsigned long)ts.tv_nsec / 20000000UL) & 0xFFU);
}

static const squid_platform_t serial_platform = {
    serial_send_char,
    serial_recv_char,
    serial_get_tick
};

/* ---- Internal helpers ---- */

static int configure_port(
    int fd,
    const struct serial_transport_config *cfg,
    struct termios *saved_tty
)
{
    struct termios tty;

    if (tcgetattr(fd, saved_tty) != 0) {
        return -1;
    }

    tty = *saved_tty;

    /* Baud rate. */
    cfsetispeed(&tty, map_baud_rate(cfg->baud));
    cfsetospeed(&tty, map_baud_rate(cfg->baud));

    /* Start from a clean slate: clear all size, parity, stop, and flow bits. */
    tty.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |=  (tcflag_t)(CREAD | CLOCAL);
    tty.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHOE | ISIG);
    tty.c_oflag &= ~(tcflag_t)(OPOST | ONLCR);

    /* Data bits. */
    if (cfg->databits == 7) {
        tty.c_cflag |= (tcflag_t)CS7;
    } else {
        tty.c_cflag |= (tcflag_t)CS8;
    }

    /* Parity. */
    switch (cfg->parity) {
    case serial_parity_even:
        tty.c_cflag |= (tcflag_t)PARENB;
        break;
    case serial_parity_odd:
        tty.c_cflag |= (tcflag_t)(PARENB | PARODD);
        break;
    default: /* serial_parity_none */
        break;
    }

    /* Stop bits. */
    if (cfg->stopbits == 2) {
        tty.c_cflag |= (tcflag_t)CSTOPB;
    }

    /* Flow control. */
    switch (cfg->flow) {
    case serial_flow_rtscts:
        tty.c_cflag |= (tcflag_t)CRTSCTS;
        break;
    case serial_flow_xonxoff:
        tty.c_iflag |= (tcflag_t)(IXON | IXOFF);
        break;
    default: /* serial_flow_none */
        break;
    }

    /* Non-blocking: return immediately with whatever is in the buffer. */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    return tcsetattr(fd, TCSANOW, &tty);
}

static void init_transport_fields(
    struct server_serial_transport *transport,
    const struct serial_transport_config *cfg
)
{
    memset(transport, 0, sizeof(*transport));
    transport->base.platform = serial_platform;
    transport->fd       = -1;
    transport->baud     = cfg->baud;
    transport->databits = cfg->databits;
    transport->parity   = cfg->parity;
    transport->stopbits = cfg->stopbits;
    transport->flow     = cfg->flow;
    strncpy(transport->device, cfg->device, sizeof(transport->device) - 1U);
}

/* ---- Public API ---- */

int serial_transport_open(
    struct server_serial_transport *transport,
    const struct serial_transport_config *cfg
)
{
    int fd = -1;

    if ((transport == NULL) || (cfg == NULL) || (cfg->device == NULL)) {
        return -1;
    }

    if (strlen(cfg->device) >= serial_transport_device_path_max) {
        return -1;
    }

    init_transport_fields(transport, cfg);

    fd = open(cfg->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }

    if (configure_port(fd, cfg, &transport->saved_tty) != 0) {
        close(fd);
        return -1;
    }

    transport->fd = fd;
    return 0;
}

void serial_transport_activate(struct server_serial_transport *transport)
{
    if (transport == NULL) {
        return;
    }

    transport_fd = transport->fd;
}

void serial_transport_close(struct server_serial_transport *transport)
{
    if (transport == NULL) {
        return;
    }

    if (transport->fd >= 0) {
        tcsetattr(transport->fd, TCSANOW, &transport->saved_tty);
        close(transport->fd);
        transport->fd = -1;
    }

    transport_fd = -1;
}
