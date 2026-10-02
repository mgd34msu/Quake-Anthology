#ifndef QA_APPLICATION_NATIVE_Q1_COMPOSITION_FLAGS_H
#define QA_APPLICATION_NATIVE_Q1_COMPOSITION_FLAGS_H

#include "internal.h"

bool application_native_q1_ctf_flags_configure(application_provider *, qa_error *);
bool application_native_q1_ctf_drop_flag(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_ctf_drop_rune(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_ctf_regenerate(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_ctf_announce(application_provider *, qa_actor_id,
    const char *key, qa_error *);
bool application_native_q1_ctf_weapon_parameters(application_provider *, qa_actor_id,
    qa_q1_weapon, qa_q1_weapon_parameters *, qa_error *);
bool application_native_q1_ctf_before_fire(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_ctf_attack_delay(application_provider *, qa_actor_id,
    qa_q1_weapon, float *, qa_error *);
bool application_native_q1_ctf_nail_fire(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_ctf_damage_effect(application_provider *, qa_damage_effect_stage,
    const qa_damage_request *, qa_damage_effect *, qa_error *);
bool application_native_q1_ctf_score_death(application_provider *, qa_actor_id victim,
    qa_actor_id attacker, qa_error *);

#endif
