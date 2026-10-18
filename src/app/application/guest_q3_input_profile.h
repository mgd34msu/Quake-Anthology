#ifndef QA_APPLICATION_GUEST_Q3_INPUT_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_INPUT_PROFILE_H

#include "guest_q3_movement_profile.h"

struct q3g_role;
typedef struct application_guest_input_profile {
    const application_q3_movement_profile *source;
    uint32_t client_think, run_client, client_spawn, respawn;
    uint32_t client_command;
    struct { uint32_t entry, connect, call; } bot_setup;
    int32_t *intermission_modes;
    size_t intermission_count;
    int32_t normal_mode, noclip_mode, freeze_mode, dead_mode;
    bool input_present, has_modes, has_dead_mode;
} application_guest_input_profile;

bool application_guest_input_profile_read(struct q3g_role *, qa_bytes primary,
                                           application_guest_input_profile *, qa_error *);
void application_guest_input_profile_free(application_guest_input_profile *);

#endif
