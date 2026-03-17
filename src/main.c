/*
 * main.c
 *
 * Entry point for squid-server.  Delegates immediately to
 * run_server_app() so that the real startup logic lives in a
 * testable translation unit rather than in main().
 *
 * GPL2 License (see: LICENSE)
 * copyright (c) 2026 tomaz stih
 *
 * tstih
 */

#include "app/server_app.h"

int main(int argc, char **argv)
{
    return run_server_app(argc, argv);
}
