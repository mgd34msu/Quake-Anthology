#ifndef QA_APPLICATION_GUEST_Q3_CONTROL_H
#define QA_APPLICATION_GUEST_Q3_CONTROL_H

#include "guest_input_private.h"
#include "qa/qvm_save.h"

typedef struct application_guest_q3_control application_guest_q3_control;
typedef struct application_guest_q3_control_scope {
    struct application_guest_q3_control_scope *previous;
    const qa_qvm_call *client_call;
    qa_actor_id actor;
    qa_q3_host_game_data data;
    uint32_t slot, player, movement;
    qa_bounds current_bounds, requested_bounds, accepted_bounds;
    qa_movement_posture pose;
    bool active, requested, accepted, fixed_pose, fixed_crouched, retired, cancelled;
} application_guest_q3_control_scope;

bool application_guest_q3_control_attach(q3g_role *, const application_guest_input_profile *,
    application_guest_q3_control **, qa_error *);
bool application_guest_q3_control_detach(application_guest_q3_control **, qa_error *);
bool application_guest_q3_control_supports_body(const application_guest_q3_control *);
qa_qvm_binding application_guest_q3_control_binding(const application_guest_q3_control *);
bool application_guest_q3_control_descriptor(const application_guest_q3_control *,
    qa_qvm_saved_function *, qa_error *);
/* Adopt only after the executor qualifies and reconstructs this exact idle
 * owner's saved function binding. This completes the no-fail owner update. */
void application_guest_q3_control_restore_binding(application_guest_q3_control *, qa_qvm_binding);
/* The genuine outer move owns the scope; its source slices share accepted hulls.
 * Equipment, when supplied, must come from the actual selected source owner. */
bool application_guest_q3_control_begin(application_guest_q3_control *, const qa_qvm_call *move,
    const qa_qvm_call *client,
    qa_actor_id, uint32_t slot, uint32_t player, uint32_t movement,
    const qa_movement_environment *, application_guest_q3_control_scope *, qa_error *);
bool application_guest_q3_control_end(application_guest_q3_control *,
    application_guest_q3_control_scope *, qa_error *);

#endif
