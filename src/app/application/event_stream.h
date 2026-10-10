#ifndef QA_APPLICATION_EVENT_STREAM_H
#define QA_APPLICATION_EVENT_STREAM_H

#include "qa/application.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/event_ring.h"
#include "unified_events.h"

typedef struct application_event_record {
    qa_builtin_event event;
    qa_application_q2_audience q2_audience;
} application_event_record;

typedef struct application_q2_map_event_record {
    qa_application_q2_map_event source;
    qa_application_q2_audience audience;
} application_q2_map_event_record;

typedef struct application_protocol_record {
    qa_application_protocol_event event;
    qa_application_q2_protocol_delivery q2;
} application_protocol_record;

typedef struct application_event_view {
    application_unified_event_record event;
    application_persistent_key persistent_key;
    bool persistent_remove;
    struct application_event_view *next;
} application_event_view;

typedef struct application_event_envelope {
    uint64_t id;
    qa_application_event_kind kind;
    union {
        application_event_record builtin;
        application_q2_map_event_record q2_map;
        qa_application_q3_map_event q3_map;
        qa_application_q2_player_event q2_player;
        application_protocol_record protocol;
    } raw;
    application_event_view *views, *last_view;
} application_event_envelope;

typedef struct application_event_write {
    qa_event_transaction transaction;
    application_event_envelope *envelope;
    uint64_t presentation_before, simulation_before;
} application_event_write;

bool application_event_stream_create(qa_application *, size_t actor_capacity, qa_error *);
bool application_event_stream_begin(qa_application *, qa_application_event_kind,
    application_event_write *, qa_error *);
void *application_event_stream_alloc(qa_application *, size_t, size_t, qa_error *);
bool application_event_stream_commit(qa_application *, application_event_write *, qa_error *);
void application_event_stream_abort(qa_application *, application_event_write *, qa_error *);
bool application_event_stream_decline(qa_application *, const application_event_write *, bool transient, qa_error *);
bool application_event_stream_close_recipients(qa_application *, const application_event_write *,
    qa_actor_id recipient, const qa_application_q2_audience *, qa_error *);
const application_event_envelope *application_event_stream_at(const qa_application *, uint64_t);
bool application_unified_persistent_prepare(qa_application *, application_event_envelope *, qa_error *);
void application_unified_persistent_publish(qa_application *, application_event_envelope *);

#endif
