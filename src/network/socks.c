#include "qa/network.h"
#include "socket_private.h"

#include <stdlib.h>
#include <string.h>

enum { SOCKS_CREDENTIAL_BYTES = 513, SOCKS_CONTROL_LIMIT = 1024,
       SOCKS_HEADER_BYTES = 10, SOCKS_POLL_BUDGET = 256 };

typedef enum socks_phase {
    SOCKS_CONNECTING, SOCKS_GREETING_SEND, SOCKS_GREETING_READ,
    SOCKS_AUTH_SEND, SOCKS_AUTH_READ, SOCKS_ASSOCIATE_SEND,
    SOCKS_ASSOCIATE_READ, SOCKS_READY, SOCKS_FAILED
} socks_phase;

typedef struct socks_transport {
    qa_net_transport *udp;
    qa_net_address server, relay;
    qa_socket control;
    socks_phase phase;
    bool platform_started, authenticated;
    uint64_t started_ns, now_ns, timeout_ns;
    size_t limit, tx_size, tx_offset, rx_size, rx_expected, credential_size, extra_bytes;
    uint16_t local_port;
    uint8_t tx[SOCKS_CREDENTIAL_BYTES], rx[SOCKS_HEADER_BYTES];
    uint8_t credentials[SOCKS_CREDENTIAL_BYTES];
    uint8_t *datagram;
    const char *failure;
} socks_transport;

static void socks_wipe(void *memory, size_t size) {
    volatile unsigned char *bytes = memory;
    while (size--) *bytes++ = 0;
}

static void socks_close_control(socks_transport *socks) {
    if (socks->control != QA_SOCKET_INVALID) {
        qa_socket_close(socks->control);
        socks->control = QA_SOCKET_INVALID;
    }
    if (socks->platform_started) {
        qa_socket_end();
        socks->platform_started = false;
    }
    socks_wipe(socks->credentials, sizeof(socks->credentials));
    socks_wipe(socks->tx, sizeof(socks->tx));
}

static bool socks_fail(socks_transport *socks, const char *message, qa_error *error) {
    if (socks->phase != SOCKS_FAILED) {
        socks->phase = SOCKS_FAILED;
        socks->failure = message;
        socks_close_control(socks);
    }
    qa_error_set(error, QA_ERROR_IO, 0, "%s", socks->failure);
    return false;
}

static void socks_schedule(socks_transport *socks, socks_phase phase,
                            const uint8_t *bytes, size_t size, size_t reply_size) {
    memcpy(socks->tx, bytes, size);
    socks->tx_size = size;
    socks->tx_offset = 0;
    socks->rx_size = 0;
    socks->rx_expected = reply_size;
    socks->phase = phase;
}

static void socks_greeting(socks_transport *socks) {
    const uint8_t anonymous[] = {5, 1, 0};
    const uint8_t authenticated[] = {5, 2, 0, 2};
    socks_schedule(socks, SOCKS_GREETING_SEND,
                   socks->authenticated ? authenticated : anonymous,
                   socks->authenticated ? sizeof(authenticated) : sizeof(anonymous), 2);
}

static void socks_associate(socks_transport *socks) {
    const uint8_t request[] = {5, 3, 0, 1, 0, 0, 0, 0,
        (uint8_t)(socks->local_port >> 8), (uint8_t)socks->local_port};
    socks_wipe(socks->credentials, sizeof(socks->credentials));
    socks->credential_size = 0;
    socks_schedule(socks, SOCKS_ASSOCIATE_SEND, request, sizeof(request), 4);
}

static bool socks_write(socks_transport *socks, bool *complete, qa_error *error) {
    size_t remaining = socks->tx_size - socks->tx_offset;
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
#if defined(_WIN32)
    int count = send(socks->control, (const char *)socks->tx + socks->tx_offset, (int)remaining, flags);
#else
    ssize_t count = send(socks->control, socks->tx + socks->tx_offset, remaining, flags);
#endif
    *complete = false;
    if (count < 0) {
        int code = qa_socket_error();
        if (qa_socket_again(code) || qa_socket_interrupted(code)) return true;
        return socks_fail(socks, "SOCKS control write failed", error);
    }
    if (!count) return socks_fail(socks, "SOCKS control connection closed", error);
    socks->tx_offset += (size_t)count;
    if (socks->tx_offset == socks->tx_size) {
        socks_wipe(socks->tx, sizeof(socks->tx));
        *complete = true;
    }
    return true;
}

static bool socks_read(socks_transport *socks, bool *complete, qa_error *error) {
    size_t remaining = socks->rx_expected - socks->rx_size;
#if defined(_WIN32)
    int count = recv(socks->control, (char *)socks->rx + socks->rx_size, (int)remaining, 0);
#else
    ssize_t count = recv(socks->control, socks->rx + socks->rx_size, remaining, 0);
#endif
    *complete = false;
    if (count < 0) {
        int code = qa_socket_error();
        if (qa_socket_again(code) || qa_socket_interrupted(code)) return true;
        return socks_fail(socks, "SOCKS control read failed", error);
    }
    if (!count) return socks_fail(socks, "SOCKS control connection closed", error);
    socks->rx_size += (size_t)count;
    *complete = socks->rx_size == socks->rx_expected;
    return true;
}

static bool socks_control_health(socks_transport *socks, qa_error *error) {
    uint8_t buffer[SOCKS_CONTROL_LIMIT];
#if defined(_WIN32)
    int count = recv(socks->control, (char *)buffer, (int)sizeof(buffer), 0);
#else
    ssize_t count = recv(socks->control, buffer, sizeof(buffer), 0);
#endif
    if (count < 0) {
        int code = qa_socket_error();
        if (qa_socket_again(code) || qa_socket_interrupted(code)) return true;
        return socks_fail(socks, "SOCKS control connection failed", error);
    }
    if (!count) return socks_fail(socks, "SOCKS control connection closed", error);
    socks->extra_bytes += (size_t)count;
    if (socks->extra_bytes > SOCKS_CONTROL_LIMIT)
        return socks_fail(socks, "SOCKS control response exceeds limit", error);
    return true;
}

static bool socks_advance(socks_transport *socks, uint64_t now_ns, qa_error *error) {
    if (socks->phase == SOCKS_FAILED) return socks_fail(socks, socks->failure, error);
    if (now_ns < socks->now_ns) return socks_fail(socks, "SOCKS clock moved backwards", error);
    socks->now_ns = now_ns;
    if (socks->phase != SOCKS_READY && now_ns - socks->started_ns >= socks->timeout_ns)
        return socks_fail(socks, "SOCKS negotiation timed out", error);
    for (size_t step = 0; step < 32; ++step) {
        bool complete;
        switch (socks->phase) {
        case SOCKS_CONNECTING: {
            int ready = qa_socket_connect_ready(socks->control);
            if (ready < 0) {
                if (qa_socket_interrupted(qa_socket_error())) return true;
                return socks_fail(socks, "SOCKS control connection failed", error);
            }
            if (!ready) return true;
            int status = 0;
            qa_socklen length = (qa_socklen)sizeof(status);
            if (getsockopt(socks->control, SOL_SOCKET, SO_ERROR, (char *)&status, &length) != 0 || status)
                return socks_fail(socks, "SOCKS control connection failed", error);
            socks_greeting(socks);
            break;
        }
        case SOCKS_GREETING_SEND:
        case SOCKS_AUTH_SEND:
        case SOCKS_ASSOCIATE_SEND:
            if (!socks_write(socks, &complete, error)) return false;
            if (!complete) return true;
            socks->phase = socks->phase == SOCKS_GREETING_SEND ? SOCKS_GREETING_READ :
                socks->phase == SOCKS_AUTH_SEND ? SOCKS_AUTH_READ : SOCKS_ASSOCIATE_READ;
            break;
        case SOCKS_GREETING_READ:
            if (!socks_read(socks, &complete, error)) return false;
            if (!complete) return true;
            if (socks->rx[0] != 5 ||
                (socks->rx[1] != 0 && (socks->rx[1] != 2 || !socks->authenticated)))
                return socks_fail(socks, "SOCKS authentication method rejected", error);
            if (socks->rx[1] == 2) {
                socks_schedule(socks, SOCKS_AUTH_SEND, socks->credentials, socks->credential_size, 2);
                socks_wipe(socks->credentials, sizeof(socks->credentials));
                socks->credential_size = 0;
            } else socks_associate(socks);
            break;
        case SOCKS_AUTH_READ:
            if (!socks_read(socks, &complete, error)) return false;
            if (!complete) return true;
            if (socks->rx[0] != 1 || socks->rx[1] != 0)
                return socks_fail(socks, "SOCKS authentication failed", error);
            socks_associate(socks);
            break;
        case SOCKS_ASSOCIATE_READ:
            if (!socks_read(socks, &complete, error)) return false;
            if (!complete) return true;
            if (socks->rx_expected == 4) {
                if (socks->rx[0] != 5 || socks->rx[1] != 0 || socks->rx[2] != 0)
                    return socks_fail(socks, "SOCKS UDP association rejected", error);
                if (socks->rx[3] != 1)
                    return socks_fail(socks, "SOCKS relay address is not IPv4", error);
                socks->rx_expected = 10;
                break;
            }
            socks->relay.kind = QA_NET_IPV4;
            memcpy(socks->relay.host.ipv4, socks->rx + 4, 4);
            socks->relay.port = (uint16_t)((uint16_t)socks->rx[8] << 8 | socks->rx[9]);
            if (!socks->relay.port) return socks_fail(socks, "SOCKS relay port is zero", error);
            /* Some proxies return INADDR_ANY for their control connection's host. */
            if (!socks->relay.host.ipv4[0] && !socks->relay.host.ipv4[1] &&
                !socks->relay.host.ipv4[2] && !socks->relay.host.ipv4[3])
                memcpy(socks->relay.host.ipv4, socks->server.host.ipv4, 4);
            socks->phase = SOCKS_READY;
            break;
        case SOCKS_READY:
            return socks_control_health(socks, error);
        case SOCKS_FAILED:
            return socks_fail(socks, socks->failure, error);
        }
    }
    return true;
}

static bool socks_send(void *opaque, const qa_net_address *to, qa_bytes payload, qa_error *error) {
    socks_transport *socks = opaque;
    if (socks->phase == SOCKS_FAILED) return socks_fail(socks, socks->failure, error);
    if (socks->phase != SOCKS_READY) {
        qa_error_set(error, QA_ERROR_IO, 0, "SOCKS negotiation is pending");
        return false;
    }
    if (!to || to->kind != QA_NET_IPV4 || !to->port || payload.size > socks->limit ||
        (payload.size && !payload.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid SOCKS IPv4 datagram");
        return false;
    }
    if (!socks_control_health(socks, error)) return false;
    bool broadcast = to->host.ipv4[0] == 255 && to->host.ipv4[1] == 255 &&
                     to->host.ipv4[2] == 255 && to->host.ipv4[3] == 255;
    if (broadcast) return qa_net_transport_send(socks->udp, to, payload, error);
    memset(socks->datagram, 0, SOCKS_HEADER_BYTES);
    socks->datagram[3] = 1;
    memcpy(socks->datagram + 4, to->host.ipv4, 4);
    socks->datagram[8] = (uint8_t)(to->port >> 8);
    socks->datagram[9] = (uint8_t)to->port;
    if (payload.size) memcpy(socks->datagram + SOCKS_HEADER_BYTES, payload.data, payload.size);
    return qa_net_transport_send(socks->udp, &socks->relay,
            (qa_bytes){socks->datagram, payload.size + SOCKS_HEADER_BYTES}, error);
}

static bool socks_receive(void *opaque, uint64_t now_ns, qa_net_datagram *out, qa_error *error) {
    socks_transport *socks = opaque;
    *out = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY};
    if (!socks_advance(socks, now_ns, error)) return false;
    if (socks->phase != SOCKS_READY) return true;
    for (size_t i = 0; i < SOCKS_POLL_BUDGET; ++i) {
        if (!qa_net_transport_receive(socks->udp, now_ns, out, error)) return false;
        if (out->kind != QA_NET_POLL_PACKET) return true;
        if (!qa_net_address_equal(&out->from, &socks->relay, true)) {
            /* Preserve direct LAN replies, including broadcast discovery. */
            if (out->payload.size > socks->limit) {
                out->kind = QA_NET_POLL_OVERSIZE;
                out->payload = (qa_bytes){NULL, 0};
            }
            return true;
        }
        qa_bytes bytes = out->payload;
        if (bytes.size < SOCKS_HEADER_BYTES || bytes.data[0] || bytes.data[1] ||
            bytes.data[2] || bytes.data[3] != 1) continue;
        qa_net_address from = {.kind = QA_NET_IPV4};
        memcpy(from.host.ipv4, bytes.data + 4, 4);
        from.port = (uint16_t)((uint16_t)bytes.data[8] << 8 | bytes.data[9]);
        out->from = from;
        out->payload = (qa_bytes){bytes.data + SOCKS_HEADER_BYTES, bytes.size - SOCKS_HEADER_BYTES};
        return true;
    }
    *out = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY};
    return true;
}

static void socks_close(void *opaque) {
    socks_transport *socks = opaque;
    socks_close_control(socks);
    qa_net_transport_close(socks->udp);
    free(socks->datagram);
    free(socks);
}

static bool socks_ready(const void *opaque) {
    const socks_transport *socks = opaque;
    return socks->phase == SOCKS_READY && qa_net_transport_ready(socks->udp);
}

static bool socks_valid_credential(qa_bytes bytes) {
    if (bytes.size > 255 || (bytes.size && !bytes.data)) return false;
    for (size_t i = 0; i < bytes.size; ++i) if (!bytes.data[i]) return false;
    return true;
}

bool qa_net_socks_wrap(qa_net_transport *udp, const qa_net_socks_options *options,
                       uint64_t now_ns, qa_net_transport **out, qa_error *error) {
    const qa_net_address *bound = qa_net_transport_address(udp);
    size_t limit = qa_net_transport_limit(udp);
    if (!out || !options || !bound || bound->kind != QA_NET_IPV4 || !bound->port ||
        options->server.kind != QA_NET_IPV4 || !options->server.port || limit <= SOCKS_HEADER_BYTES ||
        !socks_valid_credential(options->username) || !socks_valid_credential(options->password)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid SOCKS options or credential bytes");
        return false;
    }
    socks_transport *socks = calloc(1, sizeof(*socks));
    if (!socks) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "SOCKS transport allocation failed");
        return false;
    }
    socks->control = QA_SOCKET_INVALID;
    socks->datagram = malloc(limit);
    if (!socks->datagram) {
        free(socks);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "SOCKS datagram allocation failed");
        return false;
    }
    socks->limit = limit - SOCKS_HEADER_BYTES;
    socks->server = options->server;
    socks->local_port = bound->port;
    socks->started_ns = socks->now_ns = now_ns;
    socks->timeout_ns = options->timeout_ns ? options->timeout_ns : UINT64_C(5000000000);
    socks->authenticated = options->username.size || options->password.size;
    socks->credentials[0] = 1;
    socks->credentials[1] = (uint8_t)options->username.size;
    if (options->username.size) memcpy(socks->credentials + 2, options->username.data, options->username.size);
    socks->credentials[2 + options->username.size] = (uint8_t)options->password.size;
    if (options->password.size)
        memcpy(socks->credentials + 3 + options->username.size, options->password.data, options->password.size);
    socks->credential_size = 3 + options->username.size + options->password.size;
    if (!qa_socket_begin()) {
        socks_close(socks);
        qa_error_set(error, QA_ERROR_IO, 0, "SOCKS native socket initialization failed");
        return false;
    }
    socks->platform_started = true;
    socks->control = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socks->control == QA_SOCKET_INVALID || !qa_socket_nonblocking(socks->control)) {
        socks_close(socks);
        qa_error_set(error, QA_ERROR_IO, 0, "SOCKS control socket creation failed");
        return false;
    }
#if defined(SO_NOSIGPIPE)
    int no_signal = 1;
    if (setsockopt(socks->control, SOL_SOCKET, SO_NOSIGPIPE,
                   (const char *)&no_signal, (qa_socklen)sizeof(no_signal)) != 0) {
        socks_close(socks);
        qa_error_set(error, QA_ERROR_IO, 0, "SOCKS control socket configuration failed");
        return false;
    }
#endif
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(options->server.port);
    memcpy(&destination.sin_addr, options->server.host.ipv4, 4);
    int result = connect(socks->control, (const struct sockaddr *)&destination, (qa_socklen)sizeof(destination));
    if (result != 0) {
        int code = qa_socket_error();
        if (!qa_socket_again(code) && !qa_socket_interrupted(code)) {
            socks_close(socks);
            qa_error_set(error, QA_ERROR_IO, 0, "SOCKS control connection failed");
            return false;
        }
        socks->phase = SOCKS_CONNECTING;
    } else socks_greeting(socks);
    const qa_net_transport_ops ops = {
        .send = socks_send, .receive = socks_receive, .close = socks_close, .ready = socks_ready
    };
    if (!qa_net_transport_create(bound, (qa_net_limits){socks->limit, 256}, &ops, socks, out, error)) {
        socks_close(socks);
        return false;
    }
    socks->udp = udp;
    return true;
}
