#ifndef QA_APPLICATION_GUEST_Q3_MOVEMENT_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_MOVEMENT_PROFILE_H

#include "qa/qvm.h"

struct q3g_role;
typedef struct application_q3_source_region { uint32_t entry, join; } application_q3_source_region;
typedef struct application_q3_movement_profile {
    uint32_t entity_stride, client_stride, client_pointer;
    uint32_t movement_move, movement_slice, movement_duck, movement_global;
    uint32_t movement_mins, movement_maxs, movement_trace_callback, movement_trace_mask;
    application_q3_source_region locomotion;
    struct {
        uint32_t movement_type, health, team, flags;
        int32_t spectator_team, respawn_flag, *excluded;
        size_t excluded_count;
    } availability;
    uint32_t water_entity, water_movement, water_type_movement;
    uint32_t spawn, view;
    bool body_trace, present;
} application_q3_movement_profile;

/* GAME owns the declaration. Input and equipment borrow it independently. */
bool application_q3_movement_profile_read(struct q3g_role *, qa_bytes primary,
    application_q3_movement_profile *, qa_error *);
void application_q3_movement_profile_free(application_q3_movement_profile *);

#endif
