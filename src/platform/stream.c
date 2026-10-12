#include "qa/platform_stream.h"
#include "../network/transport/socket_events.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>

struct qa_platform_stream {
    qa_socket listener, peer;
    uint64_t source_id, peer_id, peer_generation, pending_peer;
    qa_sys_stream_action pending_action;
    size_t maximum_read, read_room;
    bool pending;
    uint8_t bytes[];
};

static atomic_uint_fast64_t next_source_id = 1;

static uint64_t source_id_take(qa_error *error)
{
    uint_fast64_t id = atomic_load_explicit(&next_source_id, memory_order_relaxed);
    while (id != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_source_id, &id, id + 1,
            memory_order_relaxed, memory_order_relaxed)) return (uint64_t)id;
    }
    qa_error_set(error, QA_ERROR_CAPACITY, 0, "Platform stream source IDs exhausted");
    return 0;
}

static bool socket_prepare(qa_socket socket)
{
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int enabled = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
        (qa_socklen)sizeof(enabled)) != 0) return false;
#endif
    return qa_socket_nonblocking(socket);
}

bool qa_platform_stream_create(uint16_t port, int backlog, size_t maximum_read,
    qa_platform_stream **out, qa_error *error)
{
    if (!maximum_read || maximum_read > SIZE_MAX - sizeof(qa_platform_stream)) {
        qa_error_set(error, QA_ERROR_CAPACITY, 0, "Platform stream receive backing is too large");
        return false;
    }
    qa_platform_stream *stream = calloc(1, sizeof(*stream) + maximum_read);
    if (!stream) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating platform stream");
        return false;
    }
    stream->listener = stream->peer = QA_SOCKET_INVALID;
    stream->maximum_read = stream->read_room = maximum_read;
    if (!qa_socket_begin()) {
        qa_error_set(error, QA_ERROR_IO, 0, "Initializing platform stream sockets");
        free(stream);
        return false;
    }
    stream->source_id = source_id_take(error);
    if (!stream->source_id) { qa_platform_stream_destroy(stream); return false; }
    stream->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
    BOOL exclusive = TRUE;
    if (stream->listener != QA_SOCKET_INVALID &&
        setsockopt(stream->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            (const char *)&exclusive, (qa_socklen)sizeof(exclusive)) != 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Reserving local platform stream socket");
        qa_platform_stream_destroy(stream);
        return false;
    }
#endif
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (stream->listener == QA_SOCKET_INVALID || !socket_prepare(stream->listener) ||
        bind(stream->listener, (struct sockaddr *)&address, (qa_socklen)sizeof(address)) != 0 ||
        listen(stream->listener, backlog) != 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opening local platform stream listener");
        qa_platform_stream_destroy(stream);
        return false;
    }
    *out = stream;
    return true;
}

void qa_platform_stream_stop_listener(qa_platform_stream *stream)
{
    if (!stream || stream->listener == QA_SOCKET_INVALID) return;
    qa_socket_close(stream->listener);
    stream->listener = QA_SOCKET_INVALID;
}

void qa_platform_stream_close_peer(qa_platform_stream *stream)
{
    if (!stream) return;
    if (stream->peer != QA_SOCKET_INVALID) qa_socket_close(stream->peer);
    stream->peer = QA_SOCKET_INVALID;
    stream->peer_id = 0;
    stream->pending = false;
    stream->read_room = stream->maximum_read;
}

void qa_platform_stream_destroy(qa_platform_stream *stream)
{
    if (!stream) return;
    qa_platform_stream_close_peer(stream);
    qa_platform_stream_stop_listener(stream);
    qa_socket_end();
    free(stream);
}

void qa_platform_stream_read_room(qa_platform_stream *stream, size_t room)
{ if (stream) stream->read_room = room < stream->maximum_read ? room : stream->maximum_read; }

uint64_t qa_platform_stream_source_id(const qa_platform_stream *stream)
{ return stream ? stream->source_id : 0; }

static void event_push(qa_platform_stream *stream, qa_platform_events *events,
    uint64_t time_ns, double wall_ms, qa_sys_stream_action action, uint64_t peer_id,
    size_t size)
{
    qa_sys_event event = {.kind = QA_PLATFORM_EVENT_STREAM, .time_ns = time_ns,
        .data.stream = {.source_id = stream->source_id, .peer_id = peer_id,
            .action = action, .accepted_wall_ms = wall_ms}};
    /* Intake already admitted this upper bound, with no intervening queue write. */
    qa_platform_events_push(events, &event, (qa_bytes){stream->bytes, size}, (qa_bytes){0});
    stream->pending = true;
    stream->pending_peer = peer_id;
    stream->pending_action = action;
}

bool qa_platform_stream_collect(qa_platform_stream *stream, qa_platform_events *events,
    uint64_t time_ns, double wall_ms, qa_error *error)
{
    (void)error;
    if (!stream || stream->pending) return true;
    if (stream->peer == QA_SOCKET_INVALID) {
        if (stream->listener == QA_SOCKET_INVALID ||
            qa_platform_events_admit(events, QA_PLATFORM_EVENT_STREAM, 0) !=
                QA_PLATFORM_EVENT_ACCEPTED) return true;
        if (stream->peer_generation == UINT64_MAX) {
            qa_platform_stream_stop_listener(stream);
            event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_LISTENER_ERROR, 0, 0);
            return true;
        }
        qa_socket peer = accept(stream->listener, NULL, NULL);
        if (peer == QA_SOCKET_INVALID) {
            int code = qa_socket_error();
            if (!qa_socket_again(code) && !qa_socket_interrupted(code)) {
                qa_platform_stream_stop_listener(stream);
                event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_LISTENER_ERROR, 0, 0);
            }
            return true;
        }
        if (!socket_prepare(peer)) { qa_socket_close(peer); return true; }
        stream->peer = peer;
        stream->peer_id = ++stream->peer_generation;
        event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_ACCEPT, stream->peer_id, 0);
        return true;
    }
    size_t capacity = stream->read_room;
    if (capacity > INT_MAX) capacity = INT_MAX;
    if (!capacity || qa_platform_events_admit(events, QA_PLATFORM_EVENT_STREAM, capacity) !=
        QA_PLATFORM_EVENT_ACCEPTED) return true;
    size_t size = 0;
    bool eof = false;
    qa_error failure = {0};
    if (!qa_net_socket_read(stream->peer, stream->bytes, capacity, &size, &eof, &failure) || eof) {
        uint64_t peer_id = stream->peer_id;
        qa_platform_stream_close_peer(stream);
        event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_PEER_CLOSE, peer_id, 0);
    } else if (size) event_push(stream, events, time_ns, wall_ms,
        QA_SYS_STREAM_BYTES, stream->peer_id, size);
    return true;
}

void qa_platform_stream_consumed(qa_platform_stream *stream, const qa_sys_event *event)
{
    if (stream && stream->pending && event->data.stream.source_id == stream->source_id &&
        event->data.stream.peer_id == stream->pending_peer &&
        event->data.stream.action == stream->pending_action) stream->pending = false;
}

bool qa_platform_stream_write(qa_platform_stream *stream, qa_bytes bytes,
    size_t *sent, bool *open, qa_error *error)
{
    (void)error;
    *sent = 0;
    *open = stream && stream->peer != QA_SOCKET_INVALID;
    if (!*open || !bytes.size) return true;
    size_t size = bytes.size > INT_MAX ? INT_MAX : bytes.size;
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
#if defined(_WIN32)
    int count = send(stream->peer, (const char *)bytes.data, (int)size, flags);
#else
    ssize_t count = send(stream->peer, bytes.data, size, flags);
#endif
    if (count > 0) *sent = (size_t)count;
    else {
        int code = count < 0 ? qa_socket_error() : 0;
        if (count == 0 || (!qa_socket_again(code) && !qa_socket_interrupted(code))) {
            qa_platform_stream_close_peer(stream);
            *open = false;
        }
    }
    return true;
}
