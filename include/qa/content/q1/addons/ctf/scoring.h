/* Port of src/content/q1/addons/ctf/scoring.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_SCORING_H
#define QA_CONTENT_Q1_ADDONS_CTF_SCORING_H
#include "qa/content/q1/addons/ctf/state.h"
void qa_q1ad_ctf_register_combat(qa_q1ad_ctf_state *state);
void qa_q1ad_ctf_score_death(qa_q1ad_ctf_state *state, qa_actor_id victim, const qa_actor_id *attacker);
/* Damage rule closures. cause: "falling"|"fall"|other with kind. */
bool qa_q1ad_ctf_damage_before_quad_cancel(qa_q1ad_ctf_state *state, qa_actor_id target,
                                           const char *cause_kind, const char *cause_detail);
bool qa_q1ad_ctf_armor_allowed(qa_q1ad_ctf_state *state, qa_actor_id target, const qa_actor_id *attacker);
bool qa_q1ad_ctf_protection_applies(qa_q1ad_ctf_state *state, qa_actor_id target);
bool qa_q1ad_ctf_damage_before_health(qa_q1ad_ctf_state *state, qa_actor_id target,
                                      const qa_actor_id *attacker, const qa_actor_id *inflictor, double damage);
#endif
