/*
 * serial_transport.h
 *
 * POSIX serial port (TTY) transport for squid-server.
 * Opens a serial device, configures line parameters and raw mode,
 * then provides the libsquid I/O, timing, and allocator hooks.
 *
 * NOTES:
 *  Only one active serial transport per process is supported because
 *  the libsquid platform API has no user-data pointer.
 *  The device path is limited to serial_transport_device_path_max bytes.
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_SERIAL_TRANSPORT_H
#define SERVER_SERIAL_TRANSPORT_H

#include <termios.h>

#include "transport/server_transport.h"

/* Maximum length of a device path string, including the NUL terminator. */
#define serial_transport_device_path_max 64

/* Default line parameters. */
#define serial_transport_default_device   "/dev/ttyS0"
#define serial_transport_default_baud     9600
#define serial_transport_default_databits 8
#define serial_transport_default_stopbits 1

/* Parity constants (serial_transport_config.parity). */
#define serial_parity_none  0
#define serial_parity_even  1
#define serial_parity_odd   2

/* Flow control constants (serial_transport_config.flow). */
#define serial_flow_none    0
#define serial_flow_rtscts  1
#define serial_flow_xonxoff 2

/*
 * All line parameters for one serial port opening.
 * Pass a populated struct to serial_transport_open().
 */
struct serial_transport_config {
    const char *device;    /* path to the TTY device */
    int baud;              /* baud rate (e.g. 9600, 115200) */
    int databits;          /* 7 or 8 */
    int parity;            /* serial_parity_* */
    int stopbits;          /* 1 or 2 */
    int flow;              /* serial_flow_* */
};

/*
 * Serial transport state.
 *
 * Call serial_transport_open() to configure and open the device, then
 * serial_transport_activate() before snet_init(), and finally
 * serial_transport_close() on shutdown.
 */
struct server_serial_transport {
    struct server_transport base;                       /* must be first */
    int fd;
    char device[serial_transport_device_path_max];
    int baud;
    int databits;
    int parity;
    int stopbits;
    int flow;
    struct termios saved_tty;  /* original settings, restored on close */
};

/*
 * Open the serial device described by cfg, configure it for raw
 * non-blocking I/O with the requested line parameters, and populate
 * the transport struct.
 * Returns 0 on success, -1 on error (errno is set).
 */
int serial_transport_open(
    struct server_serial_transport *transport,
    const struct serial_transport_config *cfg
);

/*
 * Point the file-scope libsquid platform hooks at transport->fd.
 * Must be called once, after serial_transport_open() succeeds and
 * before snet_init().
 */
void serial_transport_activate(struct server_serial_transport *transport);

/*
 * Restore the original terminal settings and close the file descriptor.
 */
void serial_transport_close(struct server_serial_transport *transport);

#endif
