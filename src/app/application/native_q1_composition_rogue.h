#ifndef QA_APPLICATION_NATIVE_Q1_COMPOSITION_ROGUE_H
#define QA_APPLICATION_NATIVE_Q1_COMPOSITION_ROGUE_H

#include "internal.h"

bool application_native_q1_rogue_confirmed_damage(application_provider *,
    qa_actor_id target, qa_actor_id attacker, qa_error *);
bool application_native_q1_rogue_damage_effect(application_provider *,
    qa_damage_effect_stage, const qa_damage_request *, qa_damage_effect *, qa_error *);
bool application_native_q1_base_team_health(void *, bool *enabled, qa_error *);
bool application_native_q1_rogue_world_configure(application_provider *, qa_error *);
bool application_native_q1_rogue_tag_score(void *, qa_actor_id victim,
    qa_actor_id attacker, int32_t *points, qa_error *);
bool application_native_q1_rogue_player_died(application_provider *, qa_actor_id,
    qa_actor_id attacker, qa_error *);
bool application_native_q1_rogue_before_fire(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_rogue_attack_delay(application_provider *, qa_actor_id,
    qa_q1_weapon, float *, bool observation, qa_error *);
bool application_native_q1_rogue_after_physics(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_rogue_impulse(application_provider *, qa_actor_id,
    int32_t impulse, bool *handled, qa_error *);

#endif
