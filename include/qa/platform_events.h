#ifndef QA_PLATFORM_EVENTS_H
#define QA_PLATFORM_EVENTS_H

#include "qa/common.h"

enum {
    QA_PLATFORM_EVENT_CAPACITY = 1024,
    QA_PLATFORM_EVENT_BYTE_CAPACITY = 1024 * 1024
};

typedef enum qa_platform_event_kind {
    QA_PLATFORM_EVENT_TIME,
    QA_PLATFORM_EVENT_KEY,
    QA_PLATFORM_EVENT_CHAR,
    QA_PLATFORM_EVENT_MOUSE,
    QA_PLATFORM_EVENT_CONTROLLER_AXIS,
    QA_PLATFORM_EVENT_CONTROLLER_BUTTON,
    QA_PLATFORM_EVENT_CONSOLE_LINE,
    QA_PLATFORM_EVENT_PACKET,
    QA_PLATFORM_EVENT_WINDOW,
    QA_PLATFORM_EVENT_QUIT,
    QA_PLATFORM_EVENT_DEVICE,
    QA_PLATFORM_EVENT_INPUT_FRAME
} qa_platform_event_kind;

typedef struct qa_platform_event {
    qa_platform_event_kind kind;
    uint64_t time_ns;
    int32_t value, value2;
    uint32_t offset, length;
} qa_platform_event;

typedef struct qa_platform_events qa_platform_events;

/* One caller thread owns the fixed ring and byte arena. Create reserves all
 * storage; push, peek and consume never allocate. Payloads may include the physical
 * device/window fields or packet address needed by the existing consumer. */
qa_platform_events *qa_platform_events_create(qa_error *);
void qa_platform_events_destroy(qa_platform_events *);
/* Clears queued events, arena reservations and the dropped-event counter. */
void qa_platform_events_reset(qa_platform_events *);
/* Copies the borrowed payload. Record or arena exhaustion drops oldest events.
 * A payload larger than the entire arena is itself counted as dropped. */
void qa_platform_events_push(qa_platform_events *, qa_platform_event_kind,
    uint64_t time_ns, int32_t value, int32_t value2, qa_bytes payload);
/* False means empty. The head remains queued until consumed, so a decoder
 * waiting for a previous packet can resume without losing this one. Returned
 * bytes remain valid until push, consume, reset or destroy. */
bool qa_platform_events_peek(const qa_platform_events *, qa_platform_event *, qa_bytes *);
void qa_platform_events_consume(qa_platform_events *);
uint64_t qa_platform_events_dropped(const qa_platform_events *);
void qa_platform_events_frame(qa_platform_events *, uint64_t time_ns);

/* SDL's monotonic performance clock, expressed in nanoseconds. Sleep retains
 * SDL2's whole-millisecond resolution and rounds fractional milliseconds down. */
uint64_t qa_platform_time_ns(void);
void qa_platform_sleep_ns(uint64_t);

#endif
