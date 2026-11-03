/* Port of src/content/q1/addons/ctf/teams.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_TEAMS_H
#define QA_CONTENT_Q1_ADDONS_CTF_TEAMS_H
#include "qa/content/q1/addons/ctf/state.h"
/* team NULL = TS null. */
void qa_q1ad_ctf_set_team(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *team);
void qa_q1ad_ctf_check_team(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_check_team_lock(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1_actor *qa_q1ad_ctf_spawn_point(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_show_team_prompt(qa_q1ad_ctf_state *state, qa_actor_id actor);
/* TS observerImpulse(state, actor, forcedImpulse = null): forced NULL = null. */
bool qa_q1ad_ctf_observer_impulse(qa_q1ad_ctf_state *state, qa_actor_id actor, const double *forced_impulse);
#endif
