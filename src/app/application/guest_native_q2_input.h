#ifndef QA_APPLICATION_GUEST_NATIVE_Q2_INPUT_H
#define QA_APPLICATION_GUEST_NATIVE_Q2_INPUT_H
#include "internal.h"
#include "qa/game_q2_wire.h"

/* This borrowed stage exists only across the real synchronous ClientThink.
 * move returns an owned result from the selected movement execution. */
typedef struct application_native_q2_input_stage {
    void *context;
    qa_actor_id actor;
    uint64_t time_ns;
    bool (*current)(void *, qa_actor_id);
    bool (*move)(void *, const qa_movement_input *, const qa_movement_services *,
        qa_movement_result *, qa_error *);
    bool (*arsenal)(void *, qa_actor_id, const qa_movement_command *, uint64_t, qa_error *);
} application_native_q2_input_stage;

/* Membership uses restored Engine custody and never reads SDK storage. */
bool application_native_q2_source_client(const application_provider *, qa_actor_id);
bool application_native_q2_declared_source_client(const application_provider *,qa_actor_id);
bool application_native_q2_declared_input_prepare(struct application_native_q2 *,qa_error *);
bool application_native_q2_declared_raw_capable(const application_provider *);
bool application_native_q2_declared_input_read(application_provider *,qa_actor_id,
    qa_movement_state *,qa_error *);
bool application_native_q2_input_read(application_provider *, qa_actor_id,
    qa_q2_wire_movement *, qa_error *);
bool application_native_q2_input_think(application_provider *, qa_actor_id,
    const qa_movement_command *, const application_native_q2_input_stage *, qa_error *);
#endif
