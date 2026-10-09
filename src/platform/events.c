#include "qa/platform_events.h"

#include <stdlib.h>
#include <string.h>

struct qa_platform_events {
    qa_platform_event events[QA_PLATFORM_EVENT_CAPACITY];
    uint32_t reservations[QA_PLATFORM_EVENT_CAPACITY];
    uint8_t bytes[QA_PLATFORM_EVENT_BYTE_CAPACITY];
    uint32_t head, count, byte_head, byte_tail, byte_count;
    qa_platform_event_stats stats;
    bool quit_requested;
};

qa_platform_events *qa_platform_events_create(qa_error *error)
{
    qa_platform_events *events = calloc(1, sizeof(*events));
    if (!events) qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating platform event queue");
    return events;
}

void qa_platform_events_destroy(qa_platform_events *events)
{
    free(events);
}

void qa_platform_events_reset(qa_platform_events *events)
{
    events->head = events->count = 0;
    events->byte_head = events->byte_tail = events->byte_count = 0;
    events->stats = (qa_platform_event_stats){0};
    events->quit_requested = false;
}

static void consume_head(qa_platform_events *events)
{
    uint32_t reserved = events->reservations[events->head];
    events->byte_head = (events->byte_head + reserved) % QA_PLATFORM_EVENT_BYTE_CAPACITY;
    events->byte_count -= reserved;
    events->head = (events->head + 1) % QA_PLATFORM_EVENT_CAPACITY;
    --events->count;
}

static void increment(uint64_t *value)
{
    if (*value != UINT64_MAX) ++*value;
}

static uint32_t reservation(const qa_platform_events *events, uint32_t length)
{
    uint32_t tail = events->byte_count ? events->byte_tail : 0;
    uint32_t padding = length > QA_PLATFORM_EVENT_BYTE_CAPACITY - tail ?
        QA_PLATFORM_EVENT_BYTE_CAPACITY - tail : 0;
    return padding + length;
}

qa_platform_event_result qa_platform_events_admit(qa_platform_events *events,
    qa_platform_event_kind kind, size_t maximum_payload)
{
    if (maximum_payload > QA_PLATFORM_EVENT_BYTE_CAPACITY) {
        increment(&events->stats.oversize[kind]);
        return QA_PLATFORM_EVENT_OVERSIZE;
    }
    uint32_t reserved = reservation(events, (uint32_t)maximum_payload);
    bool records = events->count == QA_PLATFORM_EVENT_CAPACITY;
    bool bytes = reserved > QA_PLATFORM_EVENT_BYTE_CAPACITY - events->byte_count;
    if (records || bytes) {
        if (records) increment(&events->stats.full_records);
        if (bytes) increment(&events->stats.full_bytes);
        increment(&events->stats.deferred[kind]);
        return QA_PLATFORM_EVENT_FULL;
    }
    return QA_PLATFORM_EVENT_ACCEPTED;
}

qa_platform_event_result qa_platform_events_push(qa_platform_events *events, qa_platform_event_kind kind,
    uint64_t time_ns, int32_t value, int32_t value2, qa_bytes payload, qa_bytes tail)
{
    if (kind == QA_PLATFORM_EVENT_QUIT) qa_platform_events_latch_quit(events);
    size_t size = tail.size > QA_PLATFORM_EVENT_BYTE_CAPACITY ||
        payload.size > QA_PLATFORM_EVENT_BYTE_CAPACITY - tail.size ?
        QA_PLATFORM_EVENT_BYTE_CAPACITY + 1u : payload.size + tail.size;
    qa_platform_event_result result = qa_platform_events_admit(events, kind, size);
    if (result != QA_PLATFORM_EVENT_ACCEPTED) return result;
    uint32_t length = (uint32_t)size;
    uint32_t reserved = reservation(events, length);
    if (!events->byte_count) events->byte_head = events->byte_tail = 0;
    uint32_t slot = (events->head + events->count) % QA_PLATFORM_EVENT_CAPACITY;
    uint32_t offset = (events->byte_tail + reserved - length) % QA_PLATFORM_EVENT_BYTE_CAPACITY;
    if (payload.size) memmove(events->bytes + offset, payload.data, payload.size);
    if (tail.size) memmove(events->bytes + offset + payload.size, tail.data, tail.size);
    events->events[slot] = (qa_platform_event){.kind = kind, .time_ns = time_ns,
        .value = value, .value2 = value2, .offset = offset, .length = length};
    events->reservations[slot] = reserved;
    events->byte_tail = (offset + length) % QA_PLATFORM_EVENT_BYTE_CAPACITY;
    events->byte_count += reserved;
    ++events->count;
    increment(&events->stats.accepted);
    if (events->count > events->stats.peak_records) events->stats.peak_records = events->count;
    if (events->byte_count > events->stats.peak_bytes) events->stats.peak_bytes = events->byte_count;
    return QA_PLATFORM_EVENT_ACCEPTED;
}

bool qa_platform_events_pending(const qa_platform_events *events, qa_platform_event_kind kind, int32_t value)
{
    for (uint32_t i = 0; i < events->count; ++i) {
        const qa_platform_event *event = &events->events[(events->head + i) % QA_PLATFORM_EVENT_CAPACITY];
        if (event->kind == kind && event->value == value) return true;
    }
    return false;
}

qa_platform_event_stats qa_platform_events_statistics(const qa_platform_events *events)
{
    qa_platform_event_stats stats = events->stats;
    stats.records = events->count;
    stats.bytes = events->byte_count;
    return stats;
}

bool qa_platform_events_peek(const qa_platform_events *events, qa_platform_event *event, qa_bytes *payload)
{
    if (!events->count) return false;
    *event = events->events[events->head];
    *payload = (qa_bytes){.data = event->length ? events->bytes + event->offset : NULL,
        .size = event->length};
    return true;
}

void qa_platform_events_consume(qa_platform_events *events)
{
    consume_head(events);
    increment(&events->stats.consumed);
}

qa_platform_event_result qa_platform_events_frame(qa_platform_events *events, uint64_t time_ns)
{
    return qa_platform_events_push(events, QA_PLATFORM_EVENT_TIME, time_ns, 0, 0, (qa_bytes){0}, (qa_bytes){0});
}

bool qa_platform_events_quit_requested(const qa_platform_events *events)
{
    return events->quit_requested;
}

void qa_platform_events_latch_quit(qa_platform_events *events)
{
    events->quit_requested = true;
}
