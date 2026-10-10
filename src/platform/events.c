#include "qa/platform_events.h"
#include "qa/event_ring.h"

#include <stdlib.h>
#include <string.h>

typedef struct platform_event_record {
    qa_sys_event event;
    qa_event_lease *lease;
    struct platform_event_record *previous, *next;
} platform_event_record;

struct qa_platform_events {
    qa_event_ring *pages;
    platform_event_record *first, *last;
    uint32_t count, byte_count;
    qa_platform_event_stats stats;
    bool quit_requested;
};

qa_platform_events *qa_platform_events_create(qa_error *error)
{
    qa_platform_events *events = calloc(1, sizeof(*events));
    if (!events) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating platform event queue"); return NULL; }
    events->pages = qa_event_ring_create(QA_PLATFORM_EVENT_BYTE_CAPACITY, 16384,
        QA_PLATFORM_EVENT_CAPACITY, error);
    if (!events->pages) { free(events); return NULL; }
    return events;
}

void qa_platform_events_reset(qa_platform_events *events)
{
    while (events->first) {
        platform_event_record *record = events->first;
        events->first = record->next;
        qa_event_lease_release(record->lease);
    }
    events->last = NULL;
    events->count = events->byte_count = 0;
    events->stats = (qa_platform_event_stats){0};
    events->quit_requested = false;
}

void qa_platform_events_destroy(qa_platform_events *events)
{
    if (!events) return;
    qa_platform_events_reset(events);
    qa_event_ring_destroy(&events->pages);
    free(events);
}

static void increment(uint64_t *value)
{ if (*value != UINT64_MAX) ++*value; }

qa_platform_event_result qa_platform_events_admit(qa_platform_events *events,
    qa_sys_event_kind kind, size_t maximum_payload)
{
    if (maximum_payload > QA_PLATFORM_EVENT_BYTE_CAPACITY) {
        increment(&events->stats.oversize[kind]);
        return QA_PLATFORM_EVENT_OVERSIZE;
    }
    uint32_t limit = QA_PLATFORM_EVENT_CAPACITY - (kind == QA_PLATFORM_EVENT_TIME ? 0u : 1u);
    if (kind == QA_PLATFORM_EVENT_PACKET) limit -= QA_PLATFORM_EVENT_KIND_COUNT;
    bool records = events->count >= limit;
    bool bytes = false;
    if (kind != QA_PLATFORM_EVENT_TIME) {
        qa_event_capacity capacity;
        qa_event_capacity_read(events->pages, &capacity);
        size_t reserve = kind == QA_PLATFORM_EVENT_PACKET ? 32768 : 16384;
        bytes = maximum_payload + sizeof(platform_event_record) + reserve > capacity.available_bytes;
    }
    qa_event_transaction probe;
    if (!records && !bytes) {
        if (!qa_event_ring_begin(events->pages, &probe)) records = true;
        else {
            bytes = !qa_event_ring_alloc(&probe, sizeof(platform_event_record) + maximum_payload,
                _Alignof(platform_event_record), NULL);
            qa_event_ring_abort(&probe);
        }
    }
    if (records || bytes) {
        if (records) increment(&events->stats.full_records);
        if (bytes) increment(&events->stats.full_bytes);
        increment(&events->stats.deferred[kind]);
        return QA_PLATFORM_EVENT_FULL;
    }
    return QA_PLATFORM_EVENT_ACCEPTED;
}

qa_platform_event_result qa_platform_events_push(qa_platform_events *events, const qa_sys_event *event,
    qa_bytes payload, qa_bytes tail)
{
    qa_sys_event_kind kind = event->kind;
    if (kind == QA_PLATFORM_EVENT_QUIT) qa_platform_events_latch_quit(events);
    size_t size = tail.size > QA_PLATFORM_EVENT_BYTE_CAPACITY ||
        payload.size > QA_PLATFORM_EVENT_BYTE_CAPACITY - tail.size ?
        QA_PLATFORM_EVENT_BYTE_CAPACITY + 1u : payload.size + tail.size;
    qa_platform_event_result result = qa_platform_events_admit(events, kind, size);
    if (result != QA_PLATFORM_EVENT_ACCEPTED) return result;
    qa_event_transaction transaction;
    qa_event_ring_begin(events->pages, &transaction);
    platform_event_record *record = qa_event_ring_alloc(&transaction, sizeof(*record) + size,
        _Alignof(platform_event_record), NULL);
    *record = (platform_event_record){.event = *event, .previous = events->last};
    uint8_t *bytes = (uint8_t *)(record + 1);
    if (payload.size) memmove(bytes, payload.data, payload.size);
    if (tail.size) memmove(bytes + payload.size, tail.data, tail.size);
    record->event.offset = 0; record->event.length = (uint32_t)size;
    uint64_t id = qa_event_ring_commit(&transaction, record);
    record->lease = qa_event_ring_retain(events->pages, id);
    qa_event_ring_retire(events->pages, id + 1);
    if (events->last) events->last->next = record;
    else events->first = record;
    events->last = record;
    ++events->count;
    events->byte_count += (uint32_t)size;
    increment(&events->stats.accepted);
    if (events->count > events->stats.peak_records) events->stats.peak_records = events->count;
    if (events->byte_count > events->stats.peak_bytes) events->stats.peak_bytes = events->byte_count;
    return QA_PLATFORM_EVENT_ACCEPTED;
}

bool qa_platform_events_pending(const qa_platform_events *events, qa_sys_event_kind kind, int32_t value)
{
    for (const platform_event_record *record = events->first; record; record = record->next)
        if (record->event.kind == kind && record->event.data.input_frame.operation == value) return true;
    return false;
}

qa_platform_event_stats qa_platform_events_statistics(const qa_platform_events *events)
{
    qa_platform_event_stats stats = events->stats;
    stats.records = events->count;
    stats.bytes = events->byte_count;
    return stats;
}

bool qa_platform_events_read(const qa_platform_events *events, qa_platform_event_cursor *cursor,
    qa_sys_event *event, qa_bytes *payload)
{
    platform_event_record *record = cursor->started ? cursor->next : events->first;
    cursor->started = true;
    cursor->current = record;
    cursor->next = record ? record->next : NULL;
    if (!record) return false;
    *event = record->event;
    *payload = (qa_bytes){.data = event->length ? (const uint8_t *)(record + 1) : NULL,
        .size = event->length};
    return true;
}

void qa_platform_events_consume(qa_platform_events *events, qa_platform_event_cursor *cursor)
{
    platform_event_record *record = cursor->current;
    if (record->previous) record->previous->next = record->next;
    else events->first = record->next;
    if (record->next) record->next->previous = record->previous;
    else events->last = record->previous;
    --events->count;
    events->byte_count -= record->event.length;
    cursor->current = NULL;
    increment(&events->stats.consumed);
    qa_event_lease_release(record->lease);
}

qa_platform_event_result qa_platform_events_frame(qa_platform_events *events, uint64_t time_ns)
{
    return qa_platform_events_push(events, &(qa_sys_event){.kind = QA_PLATFORM_EVENT_TIME, .time_ns = time_ns},
        (qa_bytes){0}, (qa_bytes){0});
}

bool qa_platform_events_quit_requested(const qa_platform_events *events)
{ return events->quit_requested; }

void qa_platform_events_latch_quit(qa_platform_events *events)
{ events->quit_requested = true; }
