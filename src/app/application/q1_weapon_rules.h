#ifndef QA_APPLICATION_Q1_WEAPON_RULES_H
#define QA_APPLICATION_Q1_WEAPON_RULES_H

#include "qa/game_q1.h"

bool application_q1_weapon_parameters(void *, qa_actor_id, qa_q1_weapon,
                                      qa_q1_weapon_parameters *, qa_error *);
bool application_q1_weapon_observation(void *, qa_actor_id, qa_q1_weapon,
                                       qa_q1_weapon_parameters *, qa_error *);
bool application_q1_before_fire(void *, qa_actor_id, qa_q1_weapon, qa_error *);
bool application_q1_attack_delay(void *, qa_actor_id, qa_q1_weapon, float *, qa_error *);
bool application_q1_nail_fire(void *, qa_actor_id, qa_q1_weapon, qa_error *);

#endif
