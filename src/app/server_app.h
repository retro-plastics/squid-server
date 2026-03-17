/*
 * server_app.h
 *
 * Public interface for the top-level application entry point.
 * Provides run_server_app(), which is the only symbol called by
 * main().
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#ifndef SERVER_APP_H
#define SERVER_APP_H

/* Parse argv, initialise the runtime, and run the server. */
int run_server_app(int argc, char **argv);

#endif
