#include "qa/platform_events.h"

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

struct qa_platform_events {
    qa_platform_event events[QA_PLATFORM_EVENT_CAPACITY];
    uint32_t reservations[QA_PLATFORM_EVENT_CAPACITY];
    uint8_t bytes[QA_PLATFORM_EVENT_BYTE_CAPACITY];
    uint32_t head, count, byte_head, byte_tail, byte_count;
    uint64_t dropped;
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
    events->dropped = 0;
    events->quit_requested = false;
}

static void remove_oldest(qa_platform_events *events)
{
    uint32_t reserved = events->reservations[events->head];
    events->byte_head = (events->byte_head + reserved) % QA_PLATFORM_EVENT_BYTE_CAPACITY;
    events->byte_count -= reserved;
    events->head = (events->head + 1) % QA_PLATFORM_EVENT_CAPACITY;
    --events->count;
}

static uint32_t reservation(qa_platform_events *events, uint32_t length)
{
    if (!events->byte_count) events->byte_head = events->byte_tail = 0;
    uint32_t padding = length > QA_PLATFORM_EVENT_BYTE_CAPACITY - events->byte_tail ?
        QA_PLATFORM_EVENT_BYTE_CAPACITY - events->byte_tail : 0;
    return padding + length;
}

void qa_platform_events_push(qa_platform_events *events, qa_platform_event_kind kind,
    uint64_t time_ns, int32_t value, int32_t value2, qa_bytes payload)
{
    if (kind == QA_PLATFORM_EVENT_QUIT) events->quit_requested = true;
    if (payload.size > QA_PLATFORM_EVENT_BYTE_CAPACITY) {
        ++events->dropped;
        return;
    }
    uint32_t length = (uint32_t)payload.size;
    uint32_t reserved = reservation(events, length);
    while (events->count == QA_PLATFORM_EVENT_CAPACITY ||
        reserved > QA_PLATFORM_EVENT_BYTE_CAPACITY - events->byte_count) {
        remove_oldest(events);
        ++events->dropped;
        reserved = reservation(events, length);
    }
    uint32_t slot = (events->head + events->count) % QA_PLATFORM_EVENT_CAPACITY;
    uint32_t offset = (events->byte_tail + reserved - length) % QA_PLATFORM_EVENT_BYTE_CAPACITY;
    if (length) memmove(events->bytes + offset, payload.data, length);
    events->events[slot] = (qa_platform_event){.kind = kind, .time_ns = time_ns,
        .value = value, .value2 = value2, .offset = offset, .length = length};
    events->reservations[slot] = reserved;
    events->byte_tail = (offset + length) % QA_PLATFORM_EVENT_BYTE_CAPACITY;
    events->byte_count += reserved;
    ++events->count;
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
    remove_oldest(events);
}

void qa_platform_events_frame(qa_platform_events *events, uint64_t time_ns)
{
    qa_platform_events_push(events, QA_PLATFORM_EVENT_TIME, time_ns, 0, 0, (qa_bytes){0});
}

uint64_t qa_platform_events_dropped(const qa_platform_events *events)
{
    return events->dropped;
}

bool qa_platform_events_quit_requested(const qa_platform_events *events)
{
    return events->quit_requested;
}

uint64_t qa_platform_time_ns(void)
{
    uint64_t frequency = SDL_GetPerformanceFrequency(), ticks = SDL_GetPerformanceCounter();
    return ticks / frequency * UINT64_C(1000000000) +
        (uint64_t)((long double)(ticks % frequency) * 1000000000.0L / (long double)frequency);
}

void qa_platform_sleep_ns(uint64_t duration)
{
    uint64_t milliseconds = duration / UINT64_C(1000000);
    while (milliseconds > UINT32_MAX) {
        SDL_Delay(UINT32_MAX);
        milliseconds -= UINT32_MAX;
    }
    if (milliseconds) SDL_Delay((uint32_t)milliseconds);
}

double qa_platform_utc_ms(void)
{
    struct timespec now;
    if(timespec_get(&now,TIME_UTC)!=TIME_UTC) return NAN;
    return (double)now.tv_sec*1000 + (double)now.tv_nsec/1000000;
}

double qa_platform_tick_ms(void)
{
    return 1000.0/(double)SDL_GetPerformanceFrequency();
}
