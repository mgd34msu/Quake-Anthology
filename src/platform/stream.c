#include "qa/platform_stream.h"
#include "qa/id_index.h"
#include "qa/pool.h"
#include "../network/transport/socket_events.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <netinet/tcp.h>
#endif

typedef struct stream_peer {
    qa_socket socket;
    uint64_t id;
    size_t slot, read_room;
    qa_sys_stream_action pending_action;
    bool pending, connecting;
} stream_peer;

struct qa_platform_stream {
    qa_socket listener;
    uint64_t source_id, peer_generation;
    size_t maximum_read, read_room, next_collect;
    qa_pool peers;
    qa_arena peer_storage, index_storage;
    qa_id_index peer_ids;
    bool listener_pending, sockets_started;
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

static bool peer_socket_prepare(qa_socket socket)
{
    int no_delay = 1;
    return socket_prepare(socket) &&
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
            (const char *)&no_delay, (qa_socklen)sizeof(no_delay)) == 0;
}

static bool stream_address(const qa_net_address *endpoint, struct sockaddr_storage *address,
    qa_socklen *address_size, int *family, qa_error *error)
{
    if (!endpoint || (endpoint->kind != QA_NET_IPV4 && endpoint->kind != QA_NET_IPV6)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Platform stream requires an IP endpoint");
        return false;
    }
    memset(address, 0, sizeof(*address));
    if (endpoint->kind == QA_NET_IPV4) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)address;
        *family = ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(endpoint->port);
        memcpy(&ipv4->sin_addr, endpoint->host.ipv4, sizeof(ipv4->sin_addr));
        *address_size = (qa_socklen)sizeof(*ipv4);
    } else {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)address;
        *family = ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(endpoint->port);
        memcpy(&ipv6->sin6_addr, endpoint->host.ipv6.bytes, sizeof(ipv6->sin6_addr));
        ipv6->sin6_scope_id = endpoint->host.ipv6.scope;
        *address_size = (qa_socklen)sizeof(*ipv6);
    }
    return true;
}

static qa_platform_stream *stream_create(size_t peer_capacity, size_t maximum_read, qa_error *error)
{
    if (!peer_capacity || !maximum_read || maximum_read > SIZE_MAX - sizeof(qa_platform_stream)) {
        qa_error_set(error, QA_ERROR_CAPACITY, 0, "Platform stream receive backing is too large");
        return NULL;
    }
    qa_platform_stream *stream = calloc(1, sizeof(*stream) + maximum_read);
    if (!stream) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating platform stream");
        return NULL;
    }
    stream->listener = QA_SOCKET_INVALID;
    stream->maximum_read = stream->read_room = maximum_read;
    qa_arena_init(&stream->index_storage, sizeof(qa_id_index_entry));
    stream->peer_ids.storage = &stream->index_storage;
    if (!qa_pool_reserve_pages(&stream->peers, &stream->peer_storage,
            peer_capacity, sizeof(stream_peer), _Alignof(stream_peer), error)) {
        qa_platform_stream_destroy(stream);
        return NULL;
    }
    for (size_t slot = 0; slot < stream->peers.capacity; ++slot)
        *(stream_peer *)qa_pool_at(&stream->peers, slot) = (stream_peer){.socket = QA_SOCKET_INVALID};
    if (!qa_id_index_reserve(&stream->peer_ids, peer_capacity, error)) {
        qa_platform_stream_destroy(stream);
        return NULL;
    }
    qa_arena_seal(&stream->index_storage);
    if (!qa_socket_begin()) {
        qa_error_set(error, QA_ERROR_IO, 0, "Initializing platform stream sockets");
        qa_platform_stream_destroy(stream);
        return NULL;
    }
    stream->sockets_started = true;
    stream->source_id = source_id_take(error);
    if (!stream->source_id) { qa_platform_stream_destroy(stream); return NULL; }
    return stream;
}

bool qa_platform_stream_listen(const qa_net_address *endpoint, int backlog,
    size_t peer_capacity, size_t maximum_read, qa_platform_stream **out, qa_error *error)
{
    struct sockaddr_storage address;
    qa_socklen address_size;
    int family;
    if (!stream_address(endpoint, &address, &address_size, &family, error)) return false;
    qa_platform_stream *stream = stream_create(peer_capacity, maximum_read, error);
    if (!stream) return false;
    stream->listener = socket(family, SOCK_STREAM, IPPROTO_TCP);
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
    if (stream->listener == QA_SOCKET_INVALID || !socket_prepare(stream->listener) ||
        bind(stream->listener, (struct sockaddr *)&address, address_size) != 0 ||
        listen(stream->listener, backlog) != 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opening local platform stream listener");
        qa_platform_stream_destroy(stream);
        return false;
    }
    *out = stream;
    return true;
}

bool qa_platform_stream_create(uint16_t port, int backlog, size_t maximum_read,
    qa_platform_stream **out, qa_error *error)
{
    qa_net_address endpoint = {.kind = QA_NET_IPV4, .port = port,
        .host.ipv4 = {127, 0, 0, 1}};
    return qa_platform_stream_listen(&endpoint, backlog, 1, maximum_read, out, error);
}

static stream_peer *peer_find(qa_platform_stream *stream, uint64_t id)
{
    if (!stream || !id) return NULL;
    qa_id_index_entry *entry = qa_id_index_find(&stream->peer_ids, 0, id);
    return entry ? qa_pool_at(&stream->peers, entry->value) : NULL;
}

static stream_peer *peer_take(qa_platform_stream *stream, qa_socket socket)
{
    size_t slot;
    stream_peer *peer = qa_pool_take(&stream->peers, &slot);
    if (!peer) return NULL;
    *peer = (stream_peer){.socket = socket, .id = ++stream->peer_generation,
        .slot = slot, .read_room = stream->read_room};
    qa_id_index_insert(&stream->peer_ids, 0, peer->id, slot);
    return peer;
}

static uint64_t first_peer_id(qa_platform_stream *stream)
{
    if (stream)
        for (size_t slot = 0; slot < stream->peers.capacity; ++slot) {
            stream_peer *peer = qa_pool_at(&stream->peers, slot);
            if (peer->id && peer->socket != QA_SOCKET_INVALID) return peer->id;
        }
    return 0;
}

bool qa_platform_stream_connect(const qa_net_address *endpoint, size_t maximum_read,
    qa_platform_stream **out, qa_error *error)
{
    if (!endpoint || !endpoint->port) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Platform stream requires an IP endpoint");
        return false;
    }
    struct sockaddr_storage address;
    qa_socklen address_size;
    int family;
    if (!stream_address(endpoint, &address, &address_size, &family, error)) return false;
    qa_platform_stream *stream = stream_create(1, maximum_read, error);
    if (!stream) return false;
    qa_socket socket_fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd == QA_SOCKET_INVALID || !peer_socket_prepare(socket_fd)) {
        if (socket_fd != QA_SOCKET_INVALID) qa_socket_close(socket_fd);
        qa_error_set(error, QA_ERROR_IO, 0, "Opening outbound platform stream socket");
        qa_platform_stream_destroy(stream);
        return false;
    }
    stream_peer *peer = peer_take(stream, socket_fd);
    if (connect(peer->socket, (struct sockaddr *)&address, address_size) != 0) {
        int code = qa_socket_error();
        if (!qa_socket_again(code) && !qa_socket_interrupted(code)) {
            qa_error_set(error, QA_ERROR_IO, 0, "Connecting outbound platform stream");
            qa_platform_stream_destroy(stream);
            return false;
        }
    }
    peer->connecting = true;
    *out = stream;
    return true;
}

void qa_platform_stream_stop_listener(qa_platform_stream *stream)
{
    if (!stream || stream->listener == QA_SOCKET_INVALID) return;
    qa_socket_close(stream->listener);
    stream->listener = QA_SOCKET_INVALID;
}

static void peer_socket_close(stream_peer *peer)
{
    if (peer->socket != QA_SOCKET_INVALID) qa_socket_close(peer->socket);
    peer->socket = QA_SOCKET_INVALID;
    peer->connecting = false;
}

static void peer_retire(qa_platform_stream *stream, stream_peer *peer)
{
    qa_id_index_remove(&stream->peer_ids, 0, peer->id);
    size_t slot = peer->slot;
    *peer = (stream_peer){.socket = QA_SOCKET_INVALID};
    qa_pool_release(&stream->peers, slot);
}

void qa_platform_stream_peer_close(qa_platform_stream *stream, uint64_t peer_id)
{
    stream_peer *peer = peer_find(stream, peer_id);
    if (!peer) return;
    peer_socket_close(peer);
    if (!peer->pending) peer_retire(stream, peer);
}

void qa_platform_stream_close_peer(qa_platform_stream *stream)
{
    qa_platform_stream_peer_close(stream, first_peer_id(stream));
    if (stream) stream->read_room = stream->maximum_read;
}

void qa_platform_stream_destroy(qa_platform_stream *stream)
{
    if (!stream) return;
    for (size_t slot = 0; slot < stream->peers.capacity; ++slot) {
        stream_peer *peer = qa_pool_at(&stream->peers, slot);
        if (peer->id) peer_socket_close(peer);
    }
    qa_platform_stream_stop_listener(stream);
    qa_id_index_destroy(&stream->peer_ids);
    qa_arena_destroy(&stream->index_storage);
    qa_arena_destroy(&stream->peer_storage);
    if (stream->sockets_started) qa_socket_end();
    free(stream);
}

void qa_platform_stream_peer_read_room(qa_platform_stream *stream, uint64_t peer_id, size_t room)
{
    stream_peer *peer = peer_find(stream, peer_id);
    if (peer) peer->read_room = room < stream->maximum_read ? room : stream->maximum_read;
}

void qa_platform_stream_read_room(qa_platform_stream *stream, size_t room)
{
    if (!stream) return;
    stream->read_room = room < stream->maximum_read ? room : stream->maximum_read;
    qa_platform_stream_peer_read_room(stream, first_peer_id(stream), stream->read_room);
}

uint64_t qa_platform_stream_source_id(const qa_platform_stream *stream)
{ return stream ? stream->source_id : 0; }

static void event_push(qa_platform_stream *stream, qa_platform_events *events,
    uint64_t time_ns, double wall_ms, qa_sys_stream_action action, stream_peer *peer,
    size_t size)
{
    qa_sys_event event = {.kind = QA_PLATFORM_EVENT_STREAM, .time_ns = time_ns,
        .data.stream = {.source_id = stream->source_id, .peer_id = peer ? peer->id : 0,
            .action = action, .accepted_wall_ms = wall_ms}};
    /* Intake already admitted this upper bound, with no intervening queue write. */
    qa_platform_events_push(events, &event, (qa_bytes){stream->bytes, size}, (qa_bytes){0});
    if (peer) {
        peer->pending = true;
        peer->pending_action = action;
    } else stream->listener_pending = true;
}

static void peer_collect(qa_platform_stream *stream, stream_peer *peer,
    qa_platform_events *events, uint64_t time_ns, double wall_ms)
{
    if (!peer->id || peer->socket == QA_SOCKET_INVALID || peer->pending) return;
    if (peer->connecting) {
        if (qa_platform_events_admit(events, QA_PLATFORM_EVENT_STREAM, 0) !=
            QA_PLATFORM_EVENT_ACCEPTED) return;
        bool connected = false;
        qa_error failure = {0};
        if (!qa_net_socket_connected(peer->socket, &connected, &failure)) {
            peer_socket_close(peer);
            event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_PEER_CLOSE, peer, 0);
        } else if (connected) {
            peer->connecting = false;
            event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_ACCEPT, peer, 0);
        }
        return;
    }
    size_t capacity = peer->read_room;
    if (capacity > INT_MAX) capacity = INT_MAX;
    if (!capacity || qa_platform_events_admit(events, QA_PLATFORM_EVENT_STREAM, capacity) !=
        QA_PLATFORM_EVENT_ACCEPTED) return;
    size_t size = 0;
    bool eof = false;
    qa_error failure = {0};
    if (!qa_net_socket_read(peer->socket, stream->bytes, capacity, &size, &eof, &failure) || eof) {
        peer_socket_close(peer);
        event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_PEER_CLOSE, peer, 0);
    } else if (size) event_push(stream, events, time_ns, wall_ms,
        QA_SYS_STREAM_BYTES, peer, size);
}

static void listener_collect(qa_platform_stream *stream, qa_platform_events *events,
    uint64_t time_ns, double wall_ms)
{
    if (stream->listener == QA_SOCKET_INVALID || stream->listener_pending ||
        stream->peers.active == stream->peers.capacity ||
        qa_platform_events_admit(events, QA_PLATFORM_EVENT_STREAM, 0) !=
            QA_PLATFORM_EVENT_ACCEPTED) return;
    if (stream->peer_generation == UINT64_MAX) {
        qa_platform_stream_stop_listener(stream);
        event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_LISTENER_ERROR, NULL, 0);
        return;
    }
    qa_socket socket_fd = accept(stream->listener, NULL, NULL);
    if (socket_fd == QA_SOCKET_INVALID) {
        int code = qa_socket_error();
        if (!qa_socket_again(code) && !qa_socket_interrupted(code)) {
            qa_platform_stream_stop_listener(stream);
            event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_LISTENER_ERROR, NULL, 0);
        }
        return;
    }
    if (!peer_socket_prepare(socket_fd)) { qa_socket_close(socket_fd); return; }
    stream_peer *peer = peer_take(stream, socket_fd);
    event_push(stream, events, time_ns, wall_ms, QA_SYS_STREAM_ACCEPT, peer, 0);
}

bool qa_platform_stream_collect(qa_platform_stream *stream, qa_platform_events *events,
    uint64_t time_ns, double wall_ms, qa_error *error)
{
    (void)error;
    if (!stream) return true;
    size_t slot = stream->next_collect;
    for (size_t visited = 0; visited <= stream->peers.capacity; ++visited) {
        if (slot == stream->peers.capacity) listener_collect(stream, events, time_ns, wall_ms);
        else peer_collect(stream, qa_pool_at(&stream->peers, slot), events, time_ns, wall_ms);
        slot = slot == stream->peers.capacity ? 0 : slot + 1;
    }
    stream->next_collect = stream->next_collect == stream->peers.capacity ?
        0 : stream->next_collect + 1;
    return true;
}

void qa_platform_stream_consumed(qa_platform_stream *stream, const qa_sys_event *event)
{
    if (!stream || event->kind != QA_PLATFORM_EVENT_STREAM ||
        event->data.stream.source_id != stream->source_id) return;
    if (!event->data.stream.peer_id) {
        if (stream->listener_pending && event->data.stream.action == QA_SYS_STREAM_LISTENER_ERROR)
            stream->listener_pending = false;
        return;
    }
    stream_peer *peer = peer_find(stream, event->data.stream.peer_id);
    if (!peer || !peer->pending || event->data.stream.action != peer->pending_action) return;
    peer->pending = false;
    if (peer->socket == QA_SOCKET_INVALID) peer_retire(stream, peer);
}

bool qa_platform_stream_peer_write(qa_platform_stream *stream, uint64_t peer_id, qa_bytes bytes,
    size_t *sent, bool *open, qa_error *error)
{
    (void)error;
    *sent = 0;
    stream_peer *peer = peer_find(stream, peer_id);
    *open = peer && peer->socket != QA_SOCKET_INVALID;
    if (!*open || peer->connecting || !bytes.size) return true;
    size_t size = bytes.size > INT_MAX ? INT_MAX : bytes.size;
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
#if defined(_WIN32)
    int count = send(peer->socket, (const char *)bytes.data, (int)size, flags);
#else
    ssize_t count = send(peer->socket, bytes.data, size, flags);
#endif
    if (count > 0) *sent = (size_t)count;
    else {
        int code = count < 0 ? qa_socket_error() : 0;
        if (count == 0 || (!qa_socket_again(code) && !qa_socket_interrupted(code))) {
            qa_platform_stream_peer_close(stream, peer_id);
            *open = false;
        }
    }
    return true;
}

bool qa_platform_stream_write(qa_platform_stream *stream, qa_bytes bytes,
    size_t *sent, bool *open, qa_error *error)
{
    return qa_platform_stream_peer_write(stream, first_peer_id(stream), bytes, sent, open, error);
}
