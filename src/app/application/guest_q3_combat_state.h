#ifndef QA_APPLICATION_GUEST_Q3_COMBAT_STATE_H
#define QA_APPLICATION_GUEST_Q3_COMBAT_STATE_H

#include "guest_q3_combat_profile.h"
#include "qa/q3_host.h"

struct q3g_role;
typedef struct application_q3_combat_actor {
    struct q3g_role *role;
    const application_q3_combat_profile *profile;
    qa_actor_id actor;
    qa_q3_host_game_data table;
    uint32_t slot, entity, player;
} application_q3_combat_actor;

bool application_q3_combat_actor_read(struct q3g_role *,
    const application_q3_combat_profile *, qa_actor_id,
    application_q3_combat_actor *, qa_error *);
bool application_q3_combat_actor_current(const application_q3_combat_actor *, qa_error *);
bool application_q3_combat_actor_live(const application_q3_combat_actor *, bool *, qa_error *);
bool application_q3_combat_state_read(void *, qa_combat_state *, qa_error *);
bool application_q3_combat_health_write(void *, float, qa_error *);
bool application_q3_combat_armor_read(const application_q3_combat_actor *, qa_armor *, qa_error *);
bool application_q3_combat_armor_validate(void *, const qa_armor *, qa_error *);
bool application_q3_combat_armor_write(void *, const qa_armor *, qa_error *);
bool application_q3_combat_empty_armor(void *, double, qa_regular_armor *, bool *, qa_error *);
bool application_q3_combat_notarget(const application_q3_combat_actor *, bool *, qa_error *);
bool application_q3_combat_velocity(const application_q3_combat_actor *, qa_vec3 *, qa_error *);
bool application_q3_combat_word_read(const application_q3_combat_actor *, uint32_t, int32_t *, qa_error *);
bool application_q3_combat_word_write(const application_q3_combat_actor *, uint32_t, int32_t, qa_error *);

#endif
