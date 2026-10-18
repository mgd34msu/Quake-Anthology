#ifndef QA_APPLICATION_GUEST_Q3_SOURCE_POSE_H
#define QA_APPLICATION_GUEST_Q3_SOURCE_POSE_H

#include "qa/game_q3.h"

struct q3g_role;
typedef enum application_q3_source_power {
    APPLICATION_Q3_QUAD, APPLICATION_Q3_HASTE, APPLICATION_Q3_FLIGHT
} application_q3_source_power;
typedef struct application_q3_source_pose_profile {
    uint32_t client_stride, max_health, persistent_max_health;
    uint32_t powerups[3];
    bool present;
} application_q3_source_pose_profile;

bool application_q3_source_pose_profile_read(struct q3g_role *, qa_bytes primary,
    application_q3_source_pose_profile *, qa_error *);
bool application_q3_source_powerup_read(struct q3g_role *, qa_actor_id, application_q3_source_power,
    int32_t *, qa_error *);
bool application_q3_source_pose_read(void *actual_role, qa_actor_id,
    qa_q3_selected_source_pose *, qa_error *);

#endif
