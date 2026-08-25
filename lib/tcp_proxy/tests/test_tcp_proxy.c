#define _POSIX_C_SOURCE 200809L

#include "tcp_proxy_core.h"
#include "squid_server/tcp_proxy_protocol.h"

#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define test_packet_capacity 255U

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
}

static int test_capabilities(struct squid_tcp_context *context)
{
    const uint8_t request[] = { SQUID_TCP_OP_CAPABILITIES };
    uint8_t response[test_packet_capacity];
    int size = handle_squid_tcp_request(
        context, request, sizeof(request), response, sizeof(response));

    return (size == 7) && (response[1] == SQUID_TCP_STATUS_OK) &&
        (response[2] == SQUID_TCP_PROTOCOL_VERSION) &&
        ((response[3] & SQUID_TCP_FEATURE_DNS) != 0U) &&
        (response[5] == 251U) && (response[6] == 253U) ? 0 : 1;
}

static int test_denied(void)
{
    static const char host[] = "localhost";
    struct squid_tcp_context context;
    uint8_t request[4U + sizeof(host) - 1U];
    uint8_t response[8];
    int size = 0;

    init_squid_tcp_context(&context, "example.com", 100U, 100U);
    request[0] = SQUID_TCP_OP_CONNECT;
    write_u16(request + 1U, 80U);
    request[3] = (uint8_t)(sizeof(host) - 1U);
    memcpy(request + 4U, host, sizeof(host) - 1U);
    size = handle_squid_tcp_request(
        &context, request, sizeof(request), response, sizeof(response));
    free_squid_tcp_context(&context);

    return (size == 2) && (response[1] == SQUID_TCP_STATUS_DENIED) ? 0 : 1;
}

static int run_echo_server(int listener)
{
    uint8_t buffer[4];
    int client = -1;
    ssize_t size = 0;

    alarm(5U);
    client = accept(listener, NULL, NULL);
    if (client < 0) {
        return 1;
    }
    size = recv(client, buffer, sizeof(buffer), MSG_WAITALL);
    if ((size != 4) || (memcmp(buffer, "ping", 4U) != 0) ||
        (send(client, "pong", 4U, 0) != 4)) {
        close(client);
        return 1;
    }
    close(client);
    return 0;
}

static int test_connection(void)
{
    static const char host[] = "127.0.0.1";
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    struct squid_tcp_context context;
    uint8_t request[test_packet_capacity];
    uint8_t response[test_packet_capacity];
    int listener = -1;
    int size = 0;
    int failed = 0;
    int child_status = 0;
    pid_t child = -1;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if ((bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0) ||
        (listen(listener, 1) != 0) ||
        (getsockname(listener, (struct sockaddr *)&address, &address_size) != 0)) {
        close(listener);
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(listener);
        return 1;
    }
    if (child == 0) {
        int result = run_echo_server(listener);
        close(listener);
        _exit(result);
    }

    init_squid_tcp_context(&context, host, 1000U, 1000U);
    request[0] = SQUID_TCP_OP_CONNECT;
    write_u16(request + 1U, ntohs(address.sin_port));
    request[3] = (uint8_t)(sizeof(host) - 1U);
    memcpy(request + 4U, host, sizeof(host) - 1U);
    size = handle_squid_tcp_request(
        &context,
        request,
        4U + sizeof(host) - 1U,
        response,
        sizeof(response)
    );
    if ((size != 3) || (response[1] != SQUID_TCP_STATUS_OK) ||
        (response[2] != SQUID_TCP_FAMILY_IPV4)) {
        failed = 1;
        goto cleanup;
    }

    request[0] = SQUID_TCP_OP_WRITE;
    request[1] = 4U;
    memcpy(request + 2U, "ping", 4U);
    size = handle_squid_tcp_request(
        &context, request, 6U, response, sizeof(response));
    if ((size != 3) || (response[1] != SQUID_TCP_STATUS_OK) ||
        (response[2] != 4U)) {
        failed = 1;
        goto cleanup;
    }

    request[0] = SQUID_TCP_OP_READ;
    write_u16(request + 1U, 1000U);
    request[3] = 4U;
    size = handle_squid_tcp_request(
        &context, request, 4U, response, sizeof(response));
    if ((size != 8) || (response[1] != SQUID_TCP_STATUS_OK) ||
        (response[2] != 0U) || (response[3] != 4U) ||
        (memcmp(response + 4U, "pong", 4U) != 0)) {
        failed = 1;
        goto cleanup;
    }

    size = handle_squid_tcp_request(
        &context, request, 4U, response, sizeof(response));
    if ((size != 4) || (response[1] != SQUID_TCP_STATUS_OK) ||
        ((response[2] & SQUID_TCP_READ_EOF) == 0U) ||
        (response[3] != 0U)) {
        failed = 1;
    }

cleanup:
    free_squid_tcp_context(&context);
    close(listener);
    if (failed) {
        kill(child, SIGTERM);
    }
    if ((waitpid(child, &child_status, 0) != child) ||
        !WIFEXITED(child_status) || (WEXITSTATUS(child_status) != 0)) {
        failed = 1;
    }
    return failed;
}

static int test_not_connected(void)
{
    struct squid_tcp_context context;
    const uint8_t request[] = { SQUID_TCP_OP_READ, 0U, 0U, 0U };
    uint8_t response[8];
    int size = 0;

    init_squid_tcp_context(&context, NULL, 0U, 0U);
    size = handle_squid_tcp_request(
        &context, request, sizeof(request), response, sizeof(response));
    free_squid_tcp_context(&context);
    return (size == 2) &&
        (response[1] == SQUID_TCP_STATUS_NOT_CONNECTED) ? 0 : 1;
}

int main(void)
{
    struct squid_tcp_context context;
    int failed = 0;

    init_squid_tcp_context(&context, NULL, 0U, 0U);

#define RUN_TEST(test_call) \
    do { \
        if ((test_call) != 0) { \
            fprintf(stderr, "%s failed\n", #test_call); \
            failed = 1; \
        } \
    } while (0)

    RUN_TEST(test_capabilities(&context));
    RUN_TEST(test_denied());
    RUN_TEST(test_connection());
    RUN_TEST(test_not_connected());

#undef RUN_TEST

    free_squid_tcp_context(&context);
    if (failed) {
        return 1;
    }
    puts("all TCP proxy tests passed");
    return 0;
}
