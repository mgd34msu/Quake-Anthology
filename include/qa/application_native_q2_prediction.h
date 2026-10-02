#ifndef QA_APPLICATION_NATIVE_Q2_PREDICTION_H
#define QA_APPLICATION_NATIVE_Q2_PREDICTION_H

#include "qa/application.h"
#include "qa/native_host.h"

typedef enum qa_native_q2_prediction_time_kind {
    QA_NATIVE_Q2_PREDICTION_SECONDS,
    QA_NATIVE_Q2_PREDICTION_MILLISECONDS
} qa_native_q2_prediction_time_kind;

typedef struct qa_application_native_q2_prediction {
    qa_actor_id actor;
    qa_launch_role role;
    qa_actor_owner owner;
    const qa_launch_instance *launch;
    const qa_native_host *host;
    const qa_native_declaration *declaration;
    qa_native_profile profile;
    qa_native_address entity, client;
    qa_source_frame frame;
    uint64_t publication_generation, map_revision, actors_revision;
    uint32_t source_slot;
    int32_t gun_frame, weapon_state, machinegun_shots;
    qa_item_id pending_weapon;
    qa_native_q2_prediction_time_kind grenade_time_kind;
    union { double seconds; int64_t milliseconds; } grenade_time;
    int32_t animation_frame, animation_end, animation_priority;
    bool grenade_blew_up, animation_duck, animation_run;
} qa_application_native_q2_prediction;

/* Reads the independently selected original Q2 ARSENAL or CHARACTER.
 * Other implementations return found=false. Private members require the
 * actual artifact declaration; no module is invoked or saved image decoded. */
bool qa_application_native_q2_prediction_read(qa_application *, qa_actor_id,
    qa_launch_role, qa_application_native_q2_prediction *, bool *found, qa_error *);
bool qa_application_native_q2_prediction_current(qa_application *,
    const qa_application_native_q2_prediction *);

#endif
