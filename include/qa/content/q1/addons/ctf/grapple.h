/* Port of src/content/q1/addons/ctf/grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_GRAPPLE_H
#define QA_CONTENT_Q1_ADDONS_CTF_GRAPPLE_H
#include "qa/content/q1/addons/ctf/state.h"
/* new ThreewaveGrapple(game, host) over the CTF rules; heap, NULL on OOM.
 * Freed by qa_q1ad_ctf_state_free. */
qa_q1_threewave_grapple *qa_q1ad_ctf_create_grapple(qa_q1ad_ctf_state *state);
void qa_q1ad_ctf_unhook(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_hook_touch(qa_q1ad_ctf_state *state, qa_q1_actor *hook, qa_actor_id other);
bool qa_q1ad_ctf_fire_hook(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_grapple_trail(qa_q1ad_ctf_state *state, qa_actor_id actor);
#endif
