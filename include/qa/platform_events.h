#ifndef QA_PLATFORM_EVENTS_H
#define QA_PLATFORM_EVENTS_H

#include "qa/common.h"
#include "qa/platform_services.h"

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
    QA_PLATFORM_EVENT_INPUT_FRAME,
    QA_PLATFORM_EVENT_USERCMD,
    QA_PLATFORM_EVENT_CONSOLE_COMMAND,
    QA_PLATFORM_EVENT_KIND_COUNT
} qa_platform_event_kind;

typedef struct qa_platform_event {
    qa_platform_event_kind kind;
    uint64_t time_ns;
    int32_t value, value2;
    uint32_t offset, length;
} qa_platform_event;

typedef struct qa_platform_events qa_platform_events;

typedef enum qa_platform_event_result {
    QA_PLATFORM_EVENT_ACCEPTED, QA_PLATFORM_EVENT_FULL, QA_PLATFORM_EVENT_OVERSIZE
} qa_platform_event_result;
typedef struct qa_platform_event_stats {
    uint64_t accepted, consumed, full_records, full_bytes;
    uint64_t deferred[QA_PLATFORM_EVENT_KIND_COUNT], oversize[QA_PLATFORM_EVENT_KIND_COUNT];
    uint32_t records, bytes, peak_records, peak_bytes;
} qa_platform_event_stats;

/* One caller thread owns the cold-sized FIFO. Admission precedes destructive
 * source reads; a full store retains every accepted event and its bytes. */
qa_platform_events *qa_platform_events_create(qa_error *);
void qa_platform_events_destroy(qa_platform_events *);
void qa_platform_events_reset(qa_platform_events *);
qa_platform_event_result qa_platform_events_admit(qa_platform_events *,
    qa_platform_event_kind, size_t maximum_payload);
qa_platform_event_result qa_platform_events_push(qa_platform_events *, qa_platform_event_kind,
    uint64_t time_ns, int32_t value, int32_t value2, qa_bytes payload, qa_bytes tail);
bool qa_platform_events_pending(const qa_platform_events *, qa_platform_event_kind, int32_t value);
qa_platform_event_stats qa_platform_events_statistics(const qa_platform_events *);
/* False means empty. The head remains queued until consumed, so a decoder
 * waiting for a previous packet can resume without losing this one. Returned
 * bytes remain valid until their own consume, reset or destroy. */
bool qa_platform_events_peek(const qa_platform_events *, qa_platform_event *, qa_bytes *);
void qa_platform_events_consume(qa_platform_events *);
/* Quit remains observable while a consumer waits for a retained packet or
 * native settings. Reset clears the request. */
bool qa_platform_events_quit_requested(const qa_platform_events *);
void qa_platform_events_latch_quit(qa_platform_events *);
qa_platform_event_result qa_platform_events_frame(qa_platform_events *, uint64_t time_ns);

#endif
