#ifndef QA_APPLICATION_GUEST_INPUT_PRIVATE_H
#define QA_APPLICATION_GUEST_INPUT_PRIVATE_H

#include "guest_q3_private.h"
#include "guest_q3_weapons.h"

typedef struct application_guest_input_profile {
    uint32_t entity_stride, client_stride, client_pointer;
    uint32_t client_think, run_client, client_spawn, move, slice, duck;
    uint32_t locomotion_entry, locomotion_join, movement_global;
    uint32_t movement_mins, movement_maxs, movement_water;
    uint32_t movement_trace_callback, movement_trace_mask;
    int32_t *intermission_modes;
    size_t intermission_count;
    int32_t normal_mode, noclip_mode, freeze_mode;
    bool input_present, has_modes, has_locomotion, has_duck, has_body_trace;
} application_guest_input_profile;

bool application_guest_input_profile_read(q3g_role *, qa_bytes primary,
                                           application_guest_input_profile *, qa_error *);
void application_guest_input_profile_free(application_guest_input_profile *);

bool application_guest_input_attach(q3g_role *, qa_bytes primary, qa_error *);
bool application_guest_input_detach(q3g_role *, qa_error *);
/* Borrows the role's genuine weapon owner after both declarations qualify.
 * Detach drops this borrow before the role destroys that owner. */
bool application_guest_input_bind_weapons(q3g_role *, application_q3_weapons *, qa_error *);
bool application_guest_input_prepare_weapon(void *role, qa_actor_id,
    const qa_qvm_call *, application_q3_weapon_preparation *, qa_error *);
bool application_guest_input_weapon_completed(void *role, qa_actor_id, bool reached, qa_error *);
bool application_guest_input_weapon_slice(const qa_application *, qa_actor_id);
bool application_guest_input_source_weapons(const qa_application *, qa_actor_id);
bool application_control_guest_weapon_step(qa_application *, qa_actor_id,
    const qa_movement_command *, const qa_q3_player *, bool reached, qa_error *);
bool application_control_guest_equipment(qa_application *, application_provider *primary,
    qa_actor_id, application_q3_weapons *, qa_q3_equipment_motion *, qa_error *);
bool application_guest_input_checkpoint(q3g_role *, qa_buffer *, qa_error *);
/* These descriptors enumerate only this real owner. The composition owner
 * qualifies the complete executor inventory together with equipment hooks. */
bool application_guest_input_descriptors(q3g_role *, qa_qvm_saved_function [6],
    size_t *count, qa_error *);
typedef struct application_guest_input_saved {
    qa_qvm_binding bindings[6];
    size_t binding_count;
    qa_movement_command applied_command;
    qa_q3_usercmd projected_command;
    bool command_projected, input_applied;
} application_guest_input_saved;
bool application_guest_input_prepare_restore(q3g_role *, qa_bytes,
    application_guest_input_saved *, qa_error *);
/* No-fail owner adoption after whole-executor identity reconstruction. */
void application_guest_input_adopt_restore(q3g_role *, const application_guest_input_saved *);
bool application_arsenal_guest_move(qa_application *, qa_actor_id,
                                     const qa_movement_command *, bool *handled,
                                     qa_error *);
struct application_control_external_stage;
bool application_arsenal_guest_stage_move(qa_application *, qa_actor_id,
    const qa_movement_command *, const struct application_control_external_stage *, bool *, qa_error *);
bool application_guest_input_applying(const qa_application *, qa_actor_id);
bool application_guest_input_actor_idle(const qa_application *, qa_actor_id);
/* Immediate handoff borrows the controller's saved slot state and validates
 * this actual GAME's retained weapon hooks and located full-actor player. */
bool application_arsenal_guest_equipment_handoff_ready(application_provider *, qa_actor_id, qa_error *);
bool application_control_guest_complete(qa_application *, qa_actor_id,
                                         const qa_movement_command *, const qa_q3_player *,
                                         qa_error *);
typedef struct application_source_input_scope {
    application_provider *owners[7];
    size_t count;
    qa_actor_id actor;
    bool slice;
    struct application_control_mod_input *components;
    qa_movement_state working_state;
    qa_movement_command working_command;
} application_source_input_scope;
bool application_control_source_input(qa_application *, qa_actor_id,
                                       qa_movement_state *, qa_movement_command *,
                                       const qa_vec3 *absolute_aim,
                                       application_source_input_scope *, bool before,
                                       bool slice, uint64_t elapsed_ns, qa_error *);
bool application_control_source_abort(application_source_input_scope *, qa_error *);
bool application_control_move_applied(qa_application *, qa_actor_id,
                                       const qa_movement_command *, qa_movement_command *, qa_error *);

#endif
