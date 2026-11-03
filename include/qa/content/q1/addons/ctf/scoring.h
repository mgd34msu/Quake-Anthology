/* Port of src/content/q1/addons/ctf/scoring.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_SCORING_H
#define QA_CONTENT_Q1_ADDONS_CTF_SCORING_H
#include "qa/content/q1/addons/ctf/state.h"
void qa_q1ad_ctf_register_combat(qa_q1ad_ctf_state *state);
/* attacker NULL = TS null. */
void qa_q1ad_ctf_score_death(qa_q1ad_ctf_state *state, qa_actor_id victim, const qa_actor_id *attacker);
#endif
