/* Port of src/content/q1/addons/ctf/drops.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_DROPS_H
#define QA_CONTENT_Q1_ADDONS_CTF_DROPS_H
#include "qa/content/q1/addons/ctf/state.h"
void qa_q1ad_ctf_toss_ammo(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_toss_weapon(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_register_drops(qa_q1ad_ctf_state *state);
#endif
