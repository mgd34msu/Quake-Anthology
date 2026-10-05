#ifndef APPLICATION_NATIVE_Q2_WIRE_ENGINE_H
#define APPLICATION_NATIVE_Q2_WIRE_ENGINE_H
#include "guest_native_q2_private.h"
#include "qa/network_q2_session.h"
#include "qa/application_native_q2_presentation.h"

typedef struct application_native_q2_wire_row {
    qa_actor_id actor;
    uint32_t source_slot;
    uint64_t retired_ns;
    bool occupied, original;
    qa_q2_source_entity_motion motion;
} application_native_q2_wire_row;
typedef struct application_native_q2_wire_reference {
    qa_actor_id actor;
    uint32_t number;
} application_native_q2_wire_reference;
typedef struct application_native_q2_wire_engine {
    uint32_t capacity, clients;
    application_native_q2_wire_row *rows;
    application_native_q2_wire_reference *references;
    size_t reference_count, reference_capacity;
} application_native_q2_wire_engine;

bool application_native_q2_wire_begin(struct application_native_q2 *, qa_error *);
void application_native_q2_wire_destroy(application_native_q2_wire_engine **);
bool application_native_q2_wire_prepare(struct application_native_q2 *,
    const qa_application_native_q2_entity_prefix *, uint32_t count, qa_error *);
bool application_native_q2_wire_linked(struct application_native_q2 *, const qa_linked_body *, qa_error *);
void application_native_q2_wire_released(struct application_native_q2 *, qa_actor_id);
bool application_native_q2_wire_number(struct application_native_q2 *, qa_actor_id, uint32_t *, qa_error *);
bool application_native_q2_wire_admit(struct application_native_q2 *, qa_actor_id, uint32_t *, qa_error *);
void application_native_q2_wire_actor_released(qa_application *, qa_actor_id);
bool application_native_q2_wire_resource(struct application_native_q2 *, unsigned,
    const char *, uint32_t *, qa_error *);
bool application_native_q2_wire_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_wire_restore(struct application_native_q2 *, qa_bytes,
    application_native_q2_wire_engine **, qa_error *);
#endif
