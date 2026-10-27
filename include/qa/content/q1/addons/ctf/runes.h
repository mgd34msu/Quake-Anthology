/* Port of src/content/q1/addons/ctf/runes.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_RUNES_H
#define QA_CONTENT_Q1_ADDONS_CTF_RUNES_H
#include "qa/content/q1/addons/ctf/state.h"
#define QA_Q1AD_CTF_HASTE_NAIL_SPEED 2000
bool qa_q1ad_ctf_haste_interval(qa_item_id item, double *out);
void qa_q1ad_ctf_drop_rune(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_regenerate(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_start_runes(qa_q1ad_ctf_state *state);
void qa_q1ad_ctf_register_runes(qa_q1ad_ctf_state *state);
/* Damage-source effect closure (afterQuad). */
double qa_q1ad_ctf_rune_damage(qa_q1ad_ctf_state *state, const qa_actor_id *attacker,
                               qa_actor_id target, double amount);
#endif
