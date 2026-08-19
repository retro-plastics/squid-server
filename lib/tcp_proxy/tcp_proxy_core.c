#define _POSIX_C_SOURCE 200809L

#include "tcp_proxy_core.h"
#include "squid_server/tcp_proxy_protocol.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define squid_tcp_default_connect_timeout_ms 5000U
#define squid_tcp_default_write_timeout_ms 5000U
#define squid_tcp_timeout_max_ms 60000U

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8U));
}

static int write_status(
    uint8_t opcode,
    uint8_t status,
    uint8_t *response,
    size_t response_capacity
)
{
    if ((response == NULL) || (response_capacity < 2U)) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_TCP_RESPONSE_BIT);
    response[1] = status;
    return 2;
}

static void close_connection(struct squid_tcp_context *context)
{
    if (context->socket_fd >= 0) {
        close(context->socket_fd);
        context->socket_fd = -1;
    }
}

void init_squid_tcp_context(
    struct squid_tcp_context *context,
    const char *allowed_hosts,
    unsigned int connect_timeout_ms,
    unsigned int write_timeout_ms
)
{
    if (context == NULL) {
        return;
    }

    memset(context, 0, sizeof(*context));
    context->socket_fd = -1;
    context->connect_timeout_ms = (connect_timeout_ms == 0U)
        ? squid_tcp_default_connect_timeout_ms
        : connect_timeout_ms;
    context->write_timeout_ms = (write_timeout_ms == 0U)
        ? squid_tcp_default_write_timeout_ms
        : write_timeout_ms;
    if (context->connect_timeout_ms > squid_tcp_timeout_max_ms) {
        context->connect_timeout_ms = squid_tcp_timeout_max_ms;
    }
    if (context->write_timeout_ms > squid_tcp_timeout_max_ms) {
        context->write_timeout_ms = squid_tcp_timeout_max_ms;
    }

    if (allowed_hosts != NULL) {
        snprintf(
            context->allowed_hosts,
            sizeof(context->allowed_hosts),
            "%s",
            allowed_hosts
        );
    }
}

void free_squid_tcp_context(struct squid_tcp_context *context)
{
    if (context == NULL) {
        return;
    }
    close_connection(context);
    memset(context, 0, sizeof(*context));
    context->socket_fd = -1;
}

static int text_equals_case_insensitive(
    const char *left,
    size_t left_size,
    const char *right,
    size_t right_size
)
{
    size_t index = 0U;

    if (left_size != right_size) {
        return 0;
    }
    for (index = 0U; index < left_size; ++index) {
        if (tolower((unsigned char)left[index]) !=
            tolower((unsigned char)right[index])) {
            return 0;
        }
    }
    return 1;
}

static int host_is_allowed(
    const struct squid_tcp_context *context,
    const char *host,
    size_t host_size
)
{
    const char *cursor = context->allowed_hosts;

    if ((cursor[0] == '\0') ||
        ((cursor[0] == '*') && (cursor[1] == '\0'))) {
        return 1;
    }

    while (*cursor != '\0') {
        const char *start = cursor;
        const char *end = NULL;

        while (isspace((unsigned char)*start)) {
            ++start;
        }
        end = start;
        while ((*end != '\0') && (*end != ',')) {
            ++end;
        }
        while ((end > start) && isspace((unsigned char)end[-1])) {
            --end;
        }
        if (text_equals_case_insensitive(
            start, (size_t)(end - start), host, host_size)) {
            return 1;
        }

        cursor = end;
        while ((*cursor != '\0') && (*cursor != ',')) {
            ++cursor;
        }
        if (*cursor == ',') {
            ++cursor;
        }
    }
    return 0;
}

static int host_is_valid(const uint8_t *host, size_t host_size, char *text)
{
    size_t index = 0U;

    if ((host == NULL) || (host_size == 0U) ||
        (host_size > SQUID_TCP_HOST_MAX)) {
        return 0;
    }
    for (index = 0U; index < host_size; ++index) {
        if ((host[index] == '\0') || isspace((unsigned char)host[index]) ||
            (host[index] == '/') || (host[index] == '\\')) {
            return 0;
        }
        text[index] = (char)host[index];
    }
    text[host_size] = '\0';
    return 1;
}

static int set_nonblocking(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
}

static int wait_for_socket(int socket_fd, short events, unsigned int timeout_ms)
{
    struct pollfd descriptor;
    int result = 0;

    descriptor.fd = socket_fd;
    descriptor.events = events;
    descriptor.revents = 0;

    do {
        result = poll(&descriptor, 1U, (int)timeout_ms);
    } while ((result < 0) && (errno == EINTR));

    if (result <= 0) {
        return result;
    }
    if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
        errno = EIO;
        return -1;
    }
    return 1;
}

static int connect_address(
    const struct addrinfo *address,
    unsigned int timeout_ms
)
{
    int socket_fd = socket(address->ai_family, address->ai_socktype,
        address->ai_protocol);
    int result = 0;

    if (socket_fd < 0) {
        return -1;
    }
    if (set_nonblocking(socket_fd) != 0) {
        close(socket_fd);
        return -1;
    }

    result = connect(socket_fd, address->ai_addr, address->ai_addrlen);
    if ((result != 0) && (errno != EINPROGRESS)) {
        close(socket_fd);
        return -1;
    }
    if (result != 0) {
        int socket_error = 0;
        socklen_t error_size = sizeof(socket_error);

        result = wait_for_socket(socket_fd, POLLOUT, timeout_ms);
        if (result <= 0) {
            if (result == 0) {
                errno = ETIMEDOUT;
            }
            close(socket_fd);
            return -1;
        }
        if ((getsockopt(
            socket_fd,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &error_size
        ) != 0) || (socket_error != 0)) {
            if (socket_error != 0) {
                errno = socket_error;
            }
            close(socket_fd);
            return -1;
        }
    }

    return socket_fd;
}

static int handle_capabilities(
    uint8_t opcode,
    uint8_t *response,
    size_t response_capacity
)
{
    if (response_capacity < 7U) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_TCP_RESPONSE_BIT);
    response[1] = SQUID_TCP_STATUS_OK;
    response[2] = SQUID_TCP_PROTOCOL_VERSION;
    response[3] = SQUID_TCP_FEATURE_IPV4 |
        SQUID_TCP_FEATURE_IPV6 | SQUID_TCP_FEATURE_DNS;
    response[4] = SQUID_TCP_HOST_MAX;
    response[5] = response_capacity > SQUID_TCP_READ_HEADER_SIZE
        ? (uint8_t)(response_capacity - SQUID_TCP_READ_HEADER_SIZE)
        : 0U;
    response[6] = response_capacity > 2U
        ? (uint8_t)(response_capacity - 2U)
        : 0U;
    return 7;
}

static int handle_connect(
    struct squid_tcp_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    char host[SQUID_TCP_HOST_MAX + 1U];
    char service[6];
    uint16_t port = 0U;
    uint8_t host_size = 0U;
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address = NULL;
    int connected_fd = -1;
    uint8_t family = SQUID_TCP_FAMILY_UNKNOWN;
    int lookup_result = 0;

    if (request_size < 5U) {
        return write_status(
            opcode, SQUID_TCP_STATUS_BAD_REQUEST, response, response_capacity);
    }
    port = read_u16(request + 1U);
    host_size = request[3];
    if ((port == 0U) || ((size_t)host_size != request_size - 4U) ||
        !host_is_valid(request + 4U, host_size, host)) {
        return write_status(
            opcode, SQUID_TCP_STATUS_BAD_REQUEST, response, response_capacity);
    }
    if (!host_is_allowed(context, host, host_size)) {
        return write_status(
            opcode, SQUID_TCP_STATUS_DENIED, response, response_capacity);
    }

    snprintf(service, sizeof(service), "%u", (unsigned int)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    lookup_result = getaddrinfo(host, service, &hints, &addresses);
    if (lookup_result != 0) {
        return write_status(
            opcode, SQUID_TCP_STATUS_DNS_FAILED, response, response_capacity);
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        connected_fd = connect_address(address, context->connect_timeout_ms);
        if (connected_fd >= 0) {
            family = address->ai_family == AF_INET6
                ? SQUID_TCP_FAMILY_IPV6
                : SQUID_TCP_FAMILY_IPV4;
            break;
        }
    }
    freeaddrinfo(addresses);

    if (connected_fd < 0) {
        return write_status(
            opcode,
            errno == ETIMEDOUT
                ? SQUID_TCP_STATUS_TIMEOUT
                : SQUID_TCP_STATUS_CONNECT_FAILED,
            response,
            response_capacity
        );
    }
    if (response_capacity < 3U) {
        close(connected_fd);
        return -1;
    }

    close_connection(context);
    context->socket_fd = connected_fd;
    response[0] = (uint8_t)(opcode | SQUID_TCP_RESPONSE_BIT);
    response[1] = SQUID_TCP_STATUS_OK;
    response[2] = family;
    return 3;
}

static int handle_write(
    struct squid_tcp_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint8_t data_size = 0U;
    ssize_t written = 0;
    int ready = 0;

    if ((request_size < 3U) ||
        ((size_t)request[1] != request_size - 2U) || (request[1] == 0U)) {
        return write_status(
            opcode, SQUID_TCP_STATUS_BAD_REQUEST, response, response_capacity);
    }
    if (context->socket_fd < 0) {
        return write_status(
            opcode,
            SQUID_TCP_STATUS_NOT_CONNECTED,
            response,
            response_capacity
        );
    }
    data_size = request[1];

    ready = wait_for_socket(
        context->socket_fd,
        POLLOUT,
        context->write_timeout_ms
    );
    if (ready == 0) {
        return write_status(
            opcode, SQUID_TCP_STATUS_TIMEOUT, response, response_capacity);
    }
    if (ready < 0) {
        close_connection(context);
        return write_status(
            opcode, SQUID_TCP_STATUS_IO_ERROR, response, response_capacity);
    }

    written = send(context->socket_fd, request + 2U, data_size, MSG_NOSIGNAL);
    if (written <= 0) {
        if ((written < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
            return write_status(
                opcode, SQUID_TCP_STATUS_TIMEOUT, response, response_capacity);
        }
        close_connection(context);
        return write_status(
            opcode, SQUID_TCP_STATUS_IO_ERROR, response, response_capacity);
    }
    if (response_capacity < 3U) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_TCP_RESPONSE_BIT);
    response[1] = SQUID_TCP_STATUS_OK;
    response[2] = (uint8_t)written;
    return 3;
}

static int handle_read(
    struct squid_tcp_context *context,
    uint8_t opcode,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint16_t wait_ms = 0U;
    uint8_t maximum_bytes = 0U;
    size_t data_capacity = 0U;
    ssize_t received = 0;
    int ready = 0;

    if (request_size != 4U) {
        return write_status(
            opcode, SQUID_TCP_STATUS_BAD_REQUEST, response, response_capacity);
    }
    if (context->socket_fd < 0) {
        return write_status(
            opcode,
            SQUID_TCP_STATUS_NOT_CONNECTED,
            response,
            response_capacity
        );
    }
    if (response_capacity < SQUID_TCP_READ_HEADER_SIZE) {
        return -1;
    }

    wait_ms = read_u16(request + 1U);
    maximum_bytes = request[3];
    if (wait_ms > SQUID_TCP_READ_WAIT_MAX_MS) {
        return write_status(
            opcode, SQUID_TCP_STATUS_BAD_REQUEST, response, response_capacity);
    }

    ready = wait_for_socket(context->socket_fd, POLLIN, wait_ms);
    if (ready < 0) {
        close_connection(context);
        return write_status(
            opcode, SQUID_TCP_STATUS_IO_ERROR, response, response_capacity);
    }

    response[0] = (uint8_t)(opcode | SQUID_TCP_RESPONSE_BIT);
    response[1] = SQUID_TCP_STATUS_OK;
    response[2] = 0U;
    response[3] = 0U;
    if (ready == 0) {
        return SQUID_TCP_READ_HEADER_SIZE;
    }

    data_capacity = response_capacity - SQUID_TCP_READ_HEADER_SIZE;
    if ((maximum_bytes > 0U) && (data_capacity > maximum_bytes)) {
        data_capacity = maximum_bytes;
    }
    if (data_capacity > UINT8_MAX) {
        data_capacity = UINT8_MAX;
    }

    received = recv(
        context->socket_fd,
        response + SQUID_TCP_READ_HEADER_SIZE,
        data_capacity,
        0
    );
    if (received == 0) {
        response[2] = SQUID_TCP_READ_EOF;
        close_connection(context);
        return SQUID_TCP_READ_HEADER_SIZE;
    }
    if (received < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return SQUID_TCP_READ_HEADER_SIZE;
        }
        close_connection(context);
        return write_status(
            opcode, SQUID_TCP_STATUS_IO_ERROR, response, response_capacity);
    }

    response[3] = (uint8_t)received;
    return (int)(SQUID_TCP_READ_HEADER_SIZE + (size_t)received);
}

static int handle_status(
    const struct squid_tcp_context *context,
    uint8_t opcode,
    uint8_t *response,
    size_t response_capacity
)
{
    if (response_capacity < 3U) {
        return -1;
    }
    response[0] = (uint8_t)(opcode | SQUID_TCP_RESPONSE_BIT);
    response[1] = SQUID_TCP_STATUS_OK;
    response[2] = context->socket_fd >= 0 ? 1U : 0U;
    return 3;
}

int handle_squid_tcp_request(
    struct squid_tcp_context *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    uint8_t opcode = 0U;

    if ((context == NULL) || (request == NULL) || (request_size == 0U) ||
        (response == NULL) || (response_capacity < 2U)) {
        return -1;
    }

    opcode = request[0];
    switch (opcode) {
    case SQUID_TCP_OP_CAPABILITIES:
        if (request_size != 1U) {
            return write_status(
                opcode,
                SQUID_TCP_STATUS_BAD_REQUEST,
                response,
                response_capacity
            );
        }
        return handle_capabilities(opcode, response, response_capacity);
    case SQUID_TCP_OP_CONNECT:
        return handle_connect(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_TCP_OP_WRITE:
        return handle_write(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_TCP_OP_READ:
        return handle_read(
            context, opcode, request, request_size, response, response_capacity);
    case SQUID_TCP_OP_CLOSE:
        if (request_size != 1U) {
            return write_status(
                opcode,
                SQUID_TCP_STATUS_BAD_REQUEST,
                response,
                response_capacity
            );
        }
        close_connection(context);
        return write_status(
            opcode, SQUID_TCP_STATUS_OK, response, response_capacity);
    case SQUID_TCP_OP_STATUS:
        if (request_size != 1U) {
            return write_status(
                opcode,
                SQUID_TCP_STATUS_BAD_REQUEST,
                response,
                response_capacity
            );
        }
        return handle_status(context, opcode, response, response_capacity);
    default:
        return write_status(
            opcode, SQUID_TCP_STATUS_BAD_REQUEST, response, response_capacity);
    }
}
