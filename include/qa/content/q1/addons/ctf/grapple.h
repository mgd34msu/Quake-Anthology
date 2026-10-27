/* Port of src/content/q1/addons/ctf/grapple.ts. The grapple
 * constructor callbacks are exported for the equipment factory.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_GRAPPLE_H
#define QA_CONTENT_Q1_ADDONS_CTF_GRAPPLE_H
#include "qa/content/q1/addons/ctf/state.h"
typedef struct qa_q1ad_grapple_input {
  bool held, release, jump;
  qa_vec3 view_angles;
  double teleport_until;
} qa_q1ad_grapple_input;
typedef struct qa_q1ad_grapple_anchor {
  bool solid, centered, player;
} qa_q1ad_grapple_anchor;
qa_q1ad_threewave_grapple qa_q1ad_ctf_create_grapple(qa_q1ad_ctf_state *state);
void qa_q1ad_ctf_unhook(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_hook_touch(qa_q1ad_ctf_state *state, qa_q1ad_actor *hook, qa_actor_id other);
bool qa_q1ad_ctf_fire_hook(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_grapple_trail(qa_q1ad_ctf_state *state, qa_actor_id actor);
/* ThreewaveGrapple constructor callbacks. */
qa_q1ad_grapple_input qa_q1ad_ctf_grapple_input(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_vec3 qa_q1ad_ctf_grapple_aim(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_vec3 forward);
qa_q1ad_grapple_anchor qa_q1ad_ctf_grapple_anchor(qa_q1ad_ctf_state *state, qa_actor_id actor);
bool qa_q1ad_ctf_grapple_can_attach(qa_q1ad_ctf_state *state, qa_actor_id owner, qa_actor_id target);
bool qa_q1ad_ctf_grapple_can_pulse(qa_q1ad_ctf_state *state, qa_actor_id owner, qa_actor_id target);
bool qa_q1ad_ctf_grapple_can_damage(qa_q1ad_ctf_state *state, qa_actor_id target, qa_actor_id owner);
#endif
