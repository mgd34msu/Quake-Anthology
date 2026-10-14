#include "network_gtv_server.h"
#include "qa/event_ring.h"
#include "qa/id_index.h"
#include "qa/pool.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { GTV_OUTPUT_PAGE_BYTES = 4096 };

typedef enum gtv_output_kind { GTV_OUTPUT_MAGIC, GTV_OUTPUT_HELLO, GTV_OUTPUT_MESSAGE } gtv_output_kind;
typedef struct gtv_server_output {
    uint64_t recipient;
    gtv_output_kind kind;
    qa_gtv_server_opcode opcode;
    uint32_t flags;
    size_t size;
    const uint8_t *members, *bytes;
} gtv_server_output;

typedef struct gtv_server_peer {
    qa_gtv_server_stream *codec;
    qa_q2_mvd_framer *frames;
    uint8_t *encoded;
    size_t encoded_capacity, encoded_size, encoded_sent;
    size_t slot, pending_bytes;
    uint64_t id, cursor, activity_ns;
    uint32_t flags;
    bool identified, authorized, active, closing;
} gtv_server_peer;

struct frontend_gtv_server {
    frontend_gtv_server_options options;
    qa_platform_stream *stream;
    qa_event_ring *output;
    qa_pool peers;
    qa_arena peer_storage, storage;
    qa_id_index peer_ids;
    bool listening;
};

static bool server_error(qa_error *error, qa_status kind, const char *text)
{
    qa_error_set(error, kind, 0, "%s", text);
    return false;
}

static gtv_server_peer *peer_find(frontend_gtv_server *server, uint64_t id)
{
    qa_id_index_entry *entry = qa_id_index_find(&server->peer_ids, 0, id);
    return entry ? qa_pool_at(&server->peers, entry->value) : NULL;
}

static void output_retire(frontend_gtv_server *server)
{
    uint64_t next = qa_event_ring_next(server->output);
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        if (peer->id && peer->cursor < next) next = peer->cursor;
    }
    qa_event_ring_retire(server->output, next);
}

static void peer_drop(frontend_gtv_server *server, gtv_server_peer *peer,
    const qa_error *failure)
{
    uint64_t id = peer->id;
    qa_platform_stream_peer_close(server->stream, id);
    qa_id_index_remove(&server->peer_ids, 0, id);
    peer->id = 0;
    peer->pending_bytes = peer->encoded_size = peer->encoded_sent = 0;
    peer->identified = peer->authorized = peer->active = peer->closing = false;
    qa_pool_release(&server->peers, peer->slot);
    output_retire(server);
    if (failure && server->options.report)
        server->options.report(server->options.context, id, failure);
}

static void peer_capacity_drop(frontend_gtv_server *server, gtv_server_peer *peer,
    const char *message)
{
    qa_error failure = {0};
    qa_error_set(&failure, QA_ERROR_CAPACITY, 0, "%s", message);
    peer_drop(server, peer, &failure);
}

static bool record_matches(const gtv_server_output *record, const gtv_server_peer *peer)
{
    return record->recipient ? record->recipient == peer->id : record->members[peer->slot] != 0;
}

static size_t record_cost(gtv_output_kind kind, size_t body_size)
{
    return kind == GTV_OUTPUT_MAGIC ? 4 : kind == GTV_OUTPUT_HELLO ? 7 : body_size + 3;
}

static bool slowest_drop(frontend_gtv_server *server)
{
    gtv_server_peer *slowest = NULL;
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        if (peer->id && peer->cursor < qa_event_ring_next(server->output) &&
            (!slowest || peer->cursor < slowest->cursor)) slowest = peer;
    }
    if (!slowest) return false;
    peer_capacity_drop(server, slowest, "GTV viewer held the full shared output store");
    return true;
}

static bool output_append(frontend_gtv_server *server, uint64_t recipient,
    gtv_output_kind kind, qa_gtv_server_opcode opcode, uint32_t flags,
    qa_bytes body, qa_error *error)
{
    if ((body.size && !body.data) || body.size >= QA_Q2_MVD_MESSAGE_BYTES)
        return server_error(error, QA_ERROR_FORMAT, "GTV output exceeds the original message limit");
    size_t cost = record_cost(kind, body.size);
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        bool selected = recipient ? peer->id == recipient : peer->id && peer->active && !peer->closing;
        if (selected && cost > server->options.pending_bytes - peer->pending_bytes)
            peer_capacity_drop(server, peer, "GTV viewer exceeded its pending output budget");
    }
    for (;;) {
        gtv_server_peer *target = recipient ? peer_find(server, recipient) : NULL;
        if (recipient && !target)
            return server_error(error, QA_ERROR_CAPACITY, "GTV viewer no longer owns its output cursor");
        if (!recipient && !frontend_gtv_server_active_viewers(server)) return true;
        qa_event_transaction transaction;
        bool begun = qa_event_ring_begin(server->output, &transaction);
        gtv_server_output *record = begun ? qa_event_ring_alloc(&transaction,
            sizeof(*record), _Alignof(gtv_server_output), NULL) : NULL;
        uint8_t *members = record && !recipient ? qa_event_ring_alloc(&transaction,
            server->peers.capacity, 1, NULL) : NULL;
        uint8_t *bytes = record && body.size ? qa_event_ring_alloc(&transaction, body.size, 1, NULL) : NULL;
        if (record && (recipient || members) && (!body.size || bytes)) {
            *record = (gtv_server_output){.recipient = recipient, .kind = kind,
                .opcode = opcode, .flags = flags, .size = body.size, .members = members, .bytes = bytes};
            if (body.size) memcpy(bytes, body.data, body.size);
            if (members)
                for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
                    gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
                    members[slot] = peer->id && peer->active && !peer->closing;
                }
            uint64_t id = qa_event_ring_commit(&transaction, record, NULL);
            if (id) {
                for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
                    gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
                    if (peer->id && record_matches(record, peer)) peer->pending_bytes += cost;
                }
                return true;
            }
        } else if (begun) qa_event_ring_abort(&transaction);
        if (!slowest_drop(server))
            return server_error(error, QA_ERROR_CAPACITY, "GTV shared output cannot admit an original message");
    }
}

static bool peer_flush(frontend_gtv_server *server, gtv_server_peer *peer, qa_error *error)
{
    while (peer->id && peer->cursor < qa_event_ring_next(server->output)) {
        const gtv_server_output *record = qa_event_ring_at(server->output, peer->cursor);
        if (!record_matches(record, peer)) {
            ++peer->cursor;
            continue;
        }
        if (!peer->encoded_size) {
            bool okay;
            if (record->kind == GTV_OUTPUT_MAGIC) {
                qa_net_writer writer;
                qa_net_writer_init(&writer, peer->encoded, peer->encoded_capacity, error);
                okay = qa_q2_mvd_write_magic(&writer);
                peer->encoded_size = qa_net_writer_size(&writer);
            } else if (record->kind == GTV_OUTPUT_HELLO) {
                okay = qa_gtv_server_hello(peer->codec, record->flags, peer->encoded,
                    peer->encoded_capacity, &peer->encoded_size, error);
            } else okay = qa_gtv_server_message(peer->codec, record->opcode,
                (qa_bytes){record->bytes, record->size}, peer->encoded,
                peer->encoded_capacity, &peer->encoded_size, error);
            if (!okay) return false;
            size_t cost = record_cost(record->kind, record->size);
            peer->pending_bytes -= cost;
            if (peer->encoded_size > server->options.pending_bytes - peer->pending_bytes)
                return server_error(error, QA_ERROR_CAPACITY, "GTV compressed output exceeded its pending budget");
            peer->pending_bytes += peer->encoded_size;
        }
        size_t sent = 0;
        bool open = false;
        if (!qa_platform_stream_peer_write(server->stream, peer->id,
                (qa_bytes){peer->encoded + peer->encoded_sent, peer->encoded_size - peer->encoded_sent},
                &sent, &open, error)) return false;
        if (!open) return server_error(error, QA_ERROR_IO, "GTV viewer closed during output");
        peer->encoded_sent += sent;
        peer->pending_bytes -= sent;
        if (peer->encoded_sent != peer->encoded_size) return true;
        peer->encoded_size = peer->encoded_sent = 0;
        ++peer->cursor;
    }
    if (peer->id && peer->closing) peer_drop(server, peer, NULL);
    return true;
}

bool frontend_gtv_server_flush(frontend_gtv_server *server, qa_error *error)
{
    (void)error;
    if (!server) return true;
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        if (!peer->id) continue;
        qa_error failure = {0};
        if (!peer_flush(server, peer, &failure)) peer_drop(server, peer, &failure);
    }
    output_retire(server);
    return true;
}

static void peer_reject(frontend_gtv_server *server, gtv_server_peer *peer,
    qa_gtv_server_opcode opcode, const qa_error *failure)
{
    uint64_t id = peer->id;
    if (!peer->identified) {
        peer_drop(server, peer, failure);
        return;
    }
    qa_error output = {0};
    if (!output_append(server, id, GTV_OUTPUT_MESSAGE, opcode, 0, (qa_bytes){0}, &output)) {
        peer = peer_find(server, id);
        if (peer) peer_drop(server, peer, failure ? failure : &output);
        return;
    }
    peer = peer_find(server, id);
    peer->active = false;
    peer->closing = true;
    qa_platform_stream_peer_read_room(server->stream, id, 0);
    if (failure && server->options.report)
        server->options.report(server->options.context, id, failure);
}

typedef struct gtv_seed_delivery {
    frontend_gtv_server *server;
    uint64_t peer_id;
    size_t packets;
} gtv_seed_delivery;

static bool seed_append(void *context, const frontend_demo_packet *packet, qa_error *error)
{
    gtv_seed_delivery *delivery = context;
    if (packet->format != FRONTEND_DEMO_MVD)
        return server_error(error, QA_ERROR_ARGUMENT, "GTV seed requires original MVD packets");
    if (!output_append(delivery->server, delivery->peer_id, GTV_OUTPUT_MESSAGE,
            QA_GTV_DATA, 0, packet->value.message, error)) return false;
    ++delivery->packets;
    return true;
}

static bool peer_request(frontend_gtv_server *server, gtv_server_peer *peer,
    const qa_gtv_request *request, qa_error *error)
{
    if (request->kind == QA_GTV_REQUEST_HELLO) {
        if (peer->authorized)
            return server_error(error, QA_ERROR_FORMAT, "Duplicate GTV hello");
        if (*server->options.password && strcmp(server->options.password, request->password)) {
            qa_error failure = {0};
            qa_error_set(&failure, QA_ERROR_FORMAT, 0, "GTV viewer was not authorized");
            peer_reject(server, peer, QA_GTV_NO_ACCESS, &failure);
            return true;
        }
        peer->flags = request->flags & QA_GTV_DEFLATE;
        peer->authorized = true;
        return output_append(server, peer->id, GTV_OUTPUT_HELLO, QA_GTV_HELLO,
            peer->flags, (qa_bytes){0}, error);
    }
    if (!peer->authorized)
        return server_error(error, QA_ERROR_FORMAT, "GTV request preceded authorization");
    switch (request->kind) {
    case QA_GTV_REQUEST_START: {
        if (peer->active) return server_error(error, QA_ERROR_FORMAT, "GTV viewer was already streaming");
        uint64_t id = peer->id;
        if (!output_append(server, id, GTV_OUTPUT_MESSAGE, QA_GTV_START, 0, (qa_bytes){0}, error)) return false;
        gtv_seed_delivery delivery = {.server = server, .peer_id = id};
        frontend_demo_sink sink = {.owner = &delivery, .append = seed_append};
        if (!server->options.seed(server->options.context, &sink, error)) {
            peer = peer_find(server, id);
            if (peer) peer_reject(server, peer, QA_GTV_ERROR, error);
            return true;
        }
        if (!delivery.packets && !output_append(server, id, GTV_OUTPUT_MESSAGE,
                QA_GTV_DATA, 0, (qa_bytes){0}, error)) return false;
        peer = peer_find(server, id);
        if (!peer) return server_error(error, QA_ERROR_CAPACITY, "GTV viewer no longer owns its seed delivery");
        peer->active = true;
        return true;
    }
    case QA_GTV_REQUEST_STOP:
        if (!peer->active) return server_error(error, QA_ERROR_FORMAT, "GTV viewer was not streaming");
        peer->active = false;
        return output_append(server, peer->id, GTV_OUTPUT_MESSAGE, QA_GTV_STOP, 0, (qa_bytes){0}, error);
    case QA_GTV_REQUEST_PING:
        return output_append(server, peer->id, GTV_OUTPUT_MESSAGE, QA_GTV_PONG, 0, (qa_bytes){0}, error);
    case QA_GTV_REQUEST_COMMAND:
        /* STRINGCMDS is not negotiated without an actual operator command route. */
        return true;
    case QA_GTV_REQUEST_HELLO:
        break;
    }
    return server_error(error, QA_ERROR_FORMAT, "Unknown GTV request");
}

static void request_clear(qa_gtv_request *request)
{
    volatile uint8_t *bytes = (uint8_t *)request;
    for (size_t i = 0; i < sizeof(*request); ++i) bytes[i] = 0;
}

static void peer_receive(frontend_gtv_server *server, gtv_server_peer *peer,
    qa_bytes input, uint64_t time_ns)
{
    uint64_t id = peer->id;
    size_t at = 0;
    while (at < input.size && peer && !peer->closing) {
        size_t consumed = 0;
        bool present = false;
        qa_bytes message;
        qa_error failure = {0};
        if (!qa_q2_mvd_framer_push(peer->frames, (qa_bytes){input.data + at, input.size - at},
                &consumed, &present, &message, &failure)) {
            peer_reject(server, peer, QA_GTV_BAD_REQUEST, &failure);
            return;
        }
        at += consumed;
        if (!peer->identified && qa_q2_mvd_framer_identified(peer->frames)) {
            peer->identified = true;
            if (!output_append(server, id, GTV_OUTPUT_MAGIC, QA_GTV_HELLO, 0, (qa_bytes){0}, &failure)) {
                peer = peer_find(server, id);
                if (peer) peer_drop(server, peer, &failure);
                return;
            }
        }
        if (qa_q2_mvd_framer_finished(peer->frames)) {
            peer_drop(server, peer, NULL);
            return;
        }
        if (present) {
            qa_gtv_request request = {0};
            bool okay = qa_gtv_request_read(message, &request, &failure) &&
                peer_request(server, peer, &request, &failure);
            request_clear(&request);
            peer = peer_find(server, id);
            if (!peer) return;
            if (!okay) {
                peer_reject(server, peer, QA_GTV_BAD_REQUEST, &failure);
                return;
            }
            peer->activity_ns = time_ns;
        }
        if (!consumed) break;
        peer = peer_find(server, id);
    }
}

static void peer_admit(frontend_gtv_server *server, uint64_t id, uint64_t time_ns)
{
    size_t slot;
    gtv_server_peer *peer = qa_pool_take(&server->peers, &slot);
    if (!peer) {
        qa_platform_stream_peer_close(server->stream, id);
        return;
    }
    qa_error failure = {0};
    if (!qa_gtv_server_stream_reset(peer->codec, &failure)) {
        qa_pool_release(&server->peers, slot);
        qa_platform_stream_peer_close(server->stream, id);
        if (server->options.report) server->options.report(server->options.context, id, &failure);
        return;
    }
    qa_q2_mvd_framer_reset(peer->frames, true);
    peer->id = id;
    peer->slot = slot;
    peer->cursor = qa_event_ring_next(server->output);
    peer->activity_ns = time_ns;
    qa_id_index_insert(&server->peer_ids, 0, id, slot);
}

bool frontend_gtv_server_receive(frontend_gtv_server *server, const qa_sys_event *event,
    qa_bytes payload, bool *recognized, qa_error *error)
{
    *recognized = server && event->kind == QA_PLATFORM_EVENT_STREAM &&
        event->data.stream.source_id == qa_platform_stream_source_id(server->stream);
    if (!*recognized) return true;
    qa_platform_stream_consumed(server->stream, event);
    gtv_server_peer *peer = peer_find(server, event->data.stream.peer_id);
    switch (event->data.stream.action) {
    case QA_SYS_STREAM_ACCEPT:
        peer_admit(server, event->data.stream.peer_id, event->time_ns);
        break;
    case QA_SYS_STREAM_BYTES:
        if (peer) peer_receive(server, peer, payload, event->time_ns);
        break;
    case QA_SYS_STREAM_PEER_CLOSE:
        if (peer) peer_drop(server, peer, NULL);
        break;
    case QA_SYS_STREAM_LISTENER_ERROR: {
        server->listening = false;
        qa_error failure = {0};
        qa_error_set(&failure, QA_ERROR_IO, 0, "GTV listener stopped accepting viewers");
        if (server->options.report) server->options.report(server->options.context, 0, &failure);
        break;
    }
    }
    return frontend_gtv_server_flush(server, error);
}

bool frontend_gtv_server_collect(frontend_gtv_server *server, qa_platform_events *events,
    uint64_t time_ns, double wall_ms, qa_error *error)
{
    if (!server) return true;
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        uint64_t timeout = server->options.timeout_ns;
        if (!peer->authorized && server->options.ghost_timeout_ns &&
            (!timeout || server->options.ghost_timeout_ns < timeout)) timeout = server->options.ghost_timeout_ns;
        if (peer->id && timeout && time_ns >= peer->activity_ns && time_ns - peer->activity_ns >= timeout) {
            qa_error failure = {0};
            qa_error_set(&failure, QA_ERROR_IO, 0, "GTV viewer timed out");
            peer_drop(server, peer, &failure);
        }
    }
    return frontend_gtv_server_flush(server, error) &&
        qa_platform_stream_collect(server->stream, events, time_ns, wall_ms, error);
}

bool frontend_gtv_server_publish(frontend_gtv_server *server, qa_bytes packet, qa_error *error)
{
    if (!server) return true;
    return output_append(server, 0, GTV_OUTPUT_MESSAGE, QA_GTV_DATA, 0, packet, error) &&
        frontend_gtv_server_flush(server, error);
}

size_t frontend_gtv_server_active_viewers(const frontend_gtv_server *server)
{
    size_t count = 0;
    if (server)
        for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
            const gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
            if (peer->id && peer->active && !peer->closing) ++count;
        }
    return count;
}

bool frontend_gtv_server_listening(const frontend_gtv_server *server)
{ return server && server->listening; }

bool frontend_gtv_server_create(const frontend_gtv_server_options *options,
    frontend_gtv_server **out, qa_error *error)
{
    if (!options || !out || !options->seed || !options->password || !options->max_viewers ||
        options->max_viewers > INT_MAX || options->pending_bytes < 7 ||
        options->max_viewers > SIZE_MAX / options->pending_bytes)
        return server_error(error, QA_ERROR_ARGUMENT, "GTV listener requires its Source and viewer policy");
    frontend_gtv_server *server = calloc(1, sizeof(*server));
    if (!server) return server_error(error, QA_ERROR_MEMORY, "Allocating GTV listener");
    server->options = *options;
    qa_arena_init(&server->storage, sizeof(gtv_server_peer));
    server->peer_ids.storage = &server->storage;
    server->options.password = qa_arena_text(&server->storage, options->password, error);
    if (!server->options.password ||
        !qa_pool_reserve_pages(&server->peers, &server->peer_storage, options->max_viewers,
            sizeof(gtv_server_peer), _Alignof(gtv_server_peer), error)) goto failed;
    for (size_t slot = 0; slot < server->peers.capacity; ++slot)
        *(gtv_server_peer *)qa_pool_at(&server->peers, slot) = (gtv_server_peer){.slot = slot};
    if (!qa_id_index_reserve(&server->peer_ids, options->max_viewers, error)) goto failed;
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        if (!qa_gtv_server_stream_create(&peer->codec, error) ||
            !qa_q2_mvd_framer_create(true, QA_GTV_REQUEST_MESSAGE_BYTES, &peer->frames, error)) goto failed;
        peer->encoded_capacity = qa_gtv_server_message_capacity(peer->codec);
        peer->encoded = qa_arena_alloc(&server->storage, peer->encoded_capacity, 1, error);
        if (!peer->encoded) goto failed;
    }
    qa_arena_seal(&server->storage);
    size_t bytes = options->max_viewers * options->pending_bytes;
    size_t records = bytes / 3 + 1;
    size_t overhead = qa_event_ring_record_storage_size(sizeof(gtv_server_output) +
        _Alignof(gtv_server_output) - 1 + options->max_viewers);
    size_t extra = 2u * QA_Q2_MVD_MESSAGE_BYTES;
    if (overhead == SIZE_MAX || bytes > SIZE_MAX - extra ||
        records > (SIZE_MAX - bytes - extra) / overhead) {
        server_error(error, QA_ERROR_CAPACITY, "GTV output policy exceeds shared storage");
        goto failed;
    }
    server->output = qa_event_ring_create(bytes + records * overhead + extra,
        GTV_OUTPUT_PAGE_BYTES, records, error);
    if (!server->output || !qa_platform_stream_listen(&options->endpoint, (int)options->max_viewers,
            options->max_viewers, QA_GTV_REQUEST_MESSAGE_BYTES + 2, &server->stream, error)) goto failed;
    server->listening = true;
    *out = server;
    return true;
failed:
    frontend_gtv_server_destroy(server);
    return false;
}

void frontend_gtv_server_destroy(frontend_gtv_server *server)
{
    if (!server) return;
    qa_platform_stream_destroy(server->stream);
    for (size_t slot = 0; slot < server->peers.capacity; ++slot) {
        gtv_server_peer *peer = qa_pool_at(&server->peers, slot);
        qa_gtv_server_stream_destroy(peer->codec);
        qa_q2_mvd_framer_destroy(peer->frames);
    }
    qa_event_ring_destroy(&server->output);
    qa_id_index_destroy(&server->peer_ids);
    if (server->options.password) {
        volatile char *password = (volatile char *)server->options.password;
        for (size_t i = 0; password[i]; ++i) password[i] = 0;
    }
    qa_arena_destroy(&server->storage);
    qa_arena_destroy(&server->peer_storage);
    free(server);
}
