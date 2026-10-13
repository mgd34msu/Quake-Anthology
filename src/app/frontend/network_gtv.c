#include "network_gtv.h"
#include "qa/event_ring.h"
#include "qa/text.h"

#include <stdlib.h>
#include <string.h>

enum { GTV_OUTPUT_BYTES = 65536, GTV_OUTPUT_RECORDS = 256,
    GTV_OUTPUT_PAGE_BYTES = 4096, GTV_START_PACKETS = 10 };

typedef struct gtv_output_record {
    size_t size;
    uint8_t bytes[];
} gtv_output_record;

struct frontend_gtv_watch {
    qa_platform_stream *stream;
    qa_gtv_client *client;
    qa_event_ring *output;
    qa_gtv_event_fn emit;
    void *user;
    uint64_t peer, cursor, activity_ns, ping_ns;
    size_t offset, pending_bytes, busy;
    int close_reason;
    bool clock_started, hello, closing, closed, notified;
};

static bool watch_error(qa_error *error, qa_status kind, const char *message)
{
    qa_error_set(error, kind, 0, "%s", message);
    return false;
}

static void output_release(void *record)
{
    gtv_output_record *row = record;
    volatile uint8_t *bytes = row->bytes;
    for (size_t i = 0; i < row->size; ++i) bytes[i] = 0;
}

static bool output_append(void *context, qa_bytes bytes, qa_error *error)
{
    frontend_gtv_watch *watch = context;
    if (bytes.size > GTV_OUTPUT_BYTES - watch->pending_bytes)
        return watch_error(error, QA_ERROR_CAPACITY, "GTV pending output exceeds its wire limit");
    qa_event_transaction transaction;
    if (!qa_event_ring_begin(watch->output, &transaction))
        return watch_error(error, QA_ERROR_CAPACITY, "GTV pending output records are full");
    gtv_output_record *record = qa_event_ring_alloc(&transaction,
        sizeof(*record) + bytes.size, _Alignof(gtv_output_record), error);
    if (!record) {
        qa_event_ring_abort(&transaction);
        return watch_error(error, QA_ERROR_CAPACITY, "GTV pending output pages are full");
    }
    record->size = bytes.size;
    if (bytes.size) memcpy(record->bytes, bytes.data, bytes.size);
    if (!qa_event_ring_commit(&transaction, record, output_release))
        return watch_error(error, QA_ERROR_CAPACITY, "GTV pending output pages are full");
    watch->pending_bytes += bytes.size;
    return true;
}

static bool watch_finish(frontend_gtv_watch *watch, bool okay, qa_error *error)
{
    if (!okay) {
        watch->closing = true;
        if (error && error->code == QA_OK)
            watch_error(error, QA_ERROR_FORMAT, "GTV watch delivery failed");
    }
    if (watch->busy || watch->closed ||
        (!watch->closing && !qa_gtv_client_closed(watch->client))) return okay;
    watch->closed = true;
    qa_gtv_client_close(watch->client);
    qa_platform_stream_close_peer(watch->stream);
    watch->peer = 0;
    watch->cursor = qa_event_ring_next(watch->output);
    qa_event_ring_retire(watch->output, watch->cursor);
    watch->offset = watch->pending_bytes = 0;
    if (watch->notified) return okay;
    watch->notified = true;
    qa_gtv_event event = {.kind = QA_GTV_EVENT_CLOSED, .reason = watch->close_reason};
    qa_error notification = {0};
    bool notified = watch->emit(watch->user, &event, &notification);
    if (!notified && okay && error) *error = notification;
    return okay && notified;
}

static bool watch_event(void *context, const qa_gtv_event *event, qa_error *error)
{
    frontend_gtv_watch *watch = context;
    if (watch->closing) return false;
    if (event->kind == QA_GTV_EVENT_CLOSED) {
        watch->close_reason = event->reason;
        watch->closing = true;
        return true;
    }
    if (event->kind == QA_GTV_EVENT_HELLO) {
        watch->hello = true;
        watch->ping_ns = watch->activity_ns;
        if (!qa_gtv_client_request_start(watch->client, GTV_START_PACKETS, error)) return false;
    }
    return watch->emit(watch->user, event, error) && !watch->closing;
}

bool frontend_gtv_watch_create(const qa_net_address *endpoint, const qa_gtv_identity *identity,
    qa_gtv_event_fn emit, void *user, frontend_gtv_watch **out, qa_error *error)
{
    if (!emit || !out) return watch_error(error, QA_ERROR_ARGUMENT, "GTV watch requires its event recipient");
    frontend_gtv_watch *watch = calloc(1, sizeof(*watch));
    if (!watch) return watch_error(error, QA_ERROR_MEMORY, "Allocating GTV watch");
    watch->emit = emit;
    watch->user = user;
    watch->close_reason = -1;
    size_t overhead = qa_event_ring_record_storage_size(sizeof(gtv_output_record) +
        _Alignof(gtv_output_record) - 1);
    watch->output = qa_event_ring_create(GTV_OUTPUT_BYTES + GTV_OUTPUT_RECORDS * overhead +
        2 * GTV_OUTPUT_PAGE_BYTES, GTV_OUTPUT_PAGE_BYTES, GTV_OUTPUT_RECORDS, error);
    if (!watch->output || !qa_gtv_client_create(identity, QA_GTV_DEFLATE | QA_GTV_STRINGCMDS,
        output_append, watch, &watch->client, error) ||
        !qa_platform_stream_connect(endpoint, QA_Q2_MVD_MESSAGE_BYTES, &watch->stream, error)) {
        frontend_gtv_watch_destroy(watch);
        return false;
    }
    watch->cursor = qa_event_ring_first(watch->output);
    *out = watch;
    return true;
}

void frontend_gtv_watch_destroy(frontend_gtv_watch *watch)
{
    if (!watch) return;
    qa_gtv_client_destroy(watch->client);
    qa_platform_stream_destroy(watch->stream);
    qa_event_ring_destroy(&watch->output);
    free(watch);
}

bool frontend_gtv_watch_closed(const frontend_gtv_watch *watch)
{ return !watch || watch->closing || watch->closed || qa_gtv_client_closed(watch->client); }

bool frontend_gtv_watch_flush(frontend_gtv_watch *watch, qa_error *error)
{
    if (!watch || watch->closed) return true;
    if (watch->busy || watch->closing) return watch_finish(watch, true, error);
    while (watch->cursor < qa_event_ring_next(watch->output)) {
        const gtv_output_record *record = qa_event_ring_at(watch->output, watch->cursor);
        size_t sent = 0;
        bool open = false;
        bool okay = qa_platform_stream_write(watch->stream,
            (qa_bytes){record->bytes + watch->offset, record->size - watch->offset},
            &sent, &open, error);
        if (!okay || !open) {
            if (okay) watch_error(error, QA_ERROR_IO, "GTV peer closed during output");
            return watch_finish(watch, false, error);
        }
        watch->offset += sent;
        watch->pending_bytes -= sent;
        if (watch->offset != record->size) return true;
        ++watch->cursor;
        watch->offset = 0;
        qa_event_ring_retire(watch->output, watch->cursor);
    }
    return true;
}

bool frontend_gtv_watch_collect(frontend_gtv_watch *watch, qa_platform_events *events,
    uint64_t time_ns, double wall_ms, qa_error *error)
{
    if (!watch || watch->closed || watch->busy) return true;
    if (!watch->clock_started) {
        watch->clock_started = true;
        watch->activity_ns = watch->ping_ns = time_ns;
    }
    if (time_ns >= watch->activity_ns &&
        time_ns - watch->activity_ns >= UINT64_C(90000000000)) {
        watch_error(error, QA_ERROR_IO, "GTV transport timed out");
        return watch_finish(watch, false, error);
    }
    if (watch->hello && time_ns >= watch->ping_ns &&
        time_ns - watch->ping_ns >= UINT64_C(60000000000)) {
        bool okay = qa_gtv_client_ping(watch->client, error);
        if (!okay) return watch_finish(watch, false, error);
        watch->ping_ns = time_ns;
    }
    if (!frontend_gtv_watch_flush(watch, error)) return false;
    if (watch->closed) return true;
    bool okay = qa_platform_stream_collect(watch->stream, events, time_ns, wall_ms, error);
    return watch_finish(watch, okay, error);
}

bool frontend_gtv_watch_receive(frontend_gtv_watch *watch, const qa_sys_event *event,
    qa_bytes payload, bool *recognized, qa_error *error)
{
    *recognized = watch && event->kind == QA_PLATFORM_EVENT_STREAM &&
        event->data.stream.source_id == qa_platform_stream_source_id(watch->stream);
    if (!*recognized) return true;
    qa_platform_stream_consumed(watch->stream, event);
    if (watch->closed) return true;
    if (!watch->clock_started || event->time_ns > watch->activity_ns)
        watch->activity_ns = event->time_ns;
    watch->clock_started = true;
    bool okay = true;
    ++watch->busy;
    switch (event->data.stream.action) {
    case QA_SYS_STREAM_ACCEPT:
        watch->peer = event->data.stream.peer_id;
        okay = qa_gtv_client_start(watch->client, error);
        break;
    case QA_SYS_STREAM_BYTES:
        if (watch->peer == event->data.stream.peer_id)
            okay = qa_gtv_client_receive(watch->client, payload, watch_event, watch, error);
        break;
    case QA_SYS_STREAM_PEER_CLOSE:
        if (!watch->peer || watch->peer == event->data.stream.peer_id) {
            watch->closing = true;
            watch_error(error, QA_ERROR_IO, "GTV peer closed its stream");
            okay = false;
        }
        break;
    case QA_SYS_STREAM_LISTENER_ERROR:
        watch_error(error, QA_ERROR_IO, "GTV transport failed");
        okay = false;
        break;
    }
    --watch->busy;
    if (!okay || watch->closing || qa_gtv_client_closed(watch->client))
        return watch_finish(watch, okay, error);
    return frontend_gtv_watch_flush(watch, error);
}

bool frontend_gtv_watch_command(frontend_gtv_watch *watch, const char *text, qa_error *error)
{
    if (!watch || frontend_gtv_watch_closed(watch))
        return watch_error(error, QA_ERROR_ARGUMENT, "GTV watch is closed");
    if (!text) return watch_error(error, QA_ERROR_ARGUMENT, "GTV command has no text");
    qa_bytes input = {(const uint8_t *)text, strlen(text)};
    size_t prefix = 0, characters = 0, size = 0;
    uint32_t scalar;
    while (characters < 150 && qa_utf8_next(input, &prefix, &scalar)) ++characters;
    uint8_t source[151];
    if (!qa_text_to_source_bytes((qa_bytes){input.data, prefix}, source, sizeof(source),
        &size, error)) return false;
    ++watch->busy;
    qa_error failure = {0};
    bool okay = qa_gtv_client_command(watch->client, (const char *)source, &failure);
    --watch->busy;
    if (!okay) {
        if (error) *error = failure;
        if (failure.code == QA_ERROR_CAPACITY || failure.code == QA_ERROR_IO)
            return watch_finish(watch, false, error);
        return false;
    }
    return frontend_gtv_watch_flush(watch, error);
}
