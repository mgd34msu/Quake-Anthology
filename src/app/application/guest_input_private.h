#ifndef QA_APPLICATION_GUEST_INPUT_PRIVATE_H
#define QA_APPLICATION_GUEST_INPUT_PRIVATE_H

#include "guest_q3_private.h"

typedef struct application_guest_branch {
    uint32_t instruction;
    bool unselected;
} application_guest_branch;

typedef struct application_guest_input_profile {
    uint32_t entity_stride, client_stride, client_pointer;
    uint32_t client_think, run_client, client_spawn, move, slice;
    uint32_t locomotion_entry, locomotion_join, movement_global;
    uint32_t movement_mins, movement_maxs, movement_water;
    uint32_t weapon_dispatcher;
    uint32_t weapon_pointer_base, weapon_pointer_offset;
    uint32_t *weapon_indirections;
    size_t weapon_indirection_count;
    bool weapon_pointer_global;
    application_guest_branch *weapon_branches;
    size_t weapon_branch_count;
    int32_t *intermission_modes;
    size_t intermission_count;
    int32_t normal_mode, noclip_mode, freeze_mode;
    bool input_present, has_modes, has_locomotion, has_weapons;
} application_guest_input_profile;

bool application_guest_input_profile_read(q3g_role *, qa_bytes primary,
                                           application_guest_input_profile *, qa_error *);
void application_guest_input_profile_free(application_guest_input_profile *);

bool application_guest_input_attach(q3g_role *, qa_bytes primary, qa_error *);
bool application_guest_input_detach(q3g_role *, qa_error *);
bool application_guest_input_checkpoint(q3g_role *, qa_buffer *, qa_error *);
bool application_guest_input_restore(q3g_role *, qa_bytes, qa_error *);
bool application_arsenal_guest_move(qa_application *, qa_actor_id,
                                     const qa_movement_command *, bool *handled,
                                     qa_error *);
bool application_guest_input_applying(const qa_application *, qa_actor_id);
bool application_guest_input_actor_idle(const qa_application *, qa_actor_id);
bool application_control_guest_complete(qa_application *, qa_actor_id,
                                         const qa_movement_command *, const qa_q3_player *,
                                         qa_error *);
typedef struct application_source_input_scope {
    application_provider *owners[7];
    size_t count;
    qa_actor_id actor;
    bool slice;
} application_source_input_scope;
bool application_control_source_input(qa_application *, qa_actor_id,
                                       qa_movement_state *, qa_movement_command *,
                                       application_source_input_scope *, bool before,
                                       bool slice, uint64_t elapsed_ns, qa_error *);
bool application_control_source_abort(application_source_input_scope *, qa_error *);
bool application_control_move_applied(qa_application *, qa_actor_id,
                                       const qa_movement_command *, qa_movement_command *, qa_error *);

#endif
