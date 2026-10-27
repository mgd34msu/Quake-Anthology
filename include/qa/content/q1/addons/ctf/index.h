/* Port of src/content/q1/addons/ctf/index.ts (Q1Ctf class).
 * qa_q1ad_ctf is the CtfState struct. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_INDEX_H
#define QA_CONTENT_Q1_ADDONS_CTF_INDEX_H
#include "qa/content/q1/addons/ctf/arsenal.h"
#include "qa/content/q1/addons/ctf/state.h"
typedef qa_q1ad_ctf_state qa_q1ad_ctf;
void qa_q1ad_ctf_init(qa_q1ad_ctf *ctf, qa_q1ad_context *context, qa_q1ad_ctf_services services,
                      qa_shared_grapple_control *shared);
qa_q1ad_ctf *qa_q1ad_register_ctf(qa_q1ad_ctf *ctf, qa_q1ad_context *context,
                                  qa_q1ad_ctf_services services, qa_shared_grapple_control *shared);
void qa_q1ad_ctf_spawn_player(qa_q1ad_ctf *ctf, qa_actor_id actor, bool first_admission);
qa_q1ad_actor *qa_q1ad_ctf_select_spawn(qa_q1ad_ctf *ctf, qa_actor_id actor);
bool qa_q1ad_ctf_fall_damage_allowed(qa_q1ad_ctf *ctf, qa_actor_id actor);
void qa_q1ad_ctf_player_frame(qa_q1ad_ctf *ctf, qa_actor_id actor);
void qa_q1ad_ctf_after_physics(qa_q1ad_ctf *ctf, qa_actor_id actor);
bool qa_q1ad_ctf_impulse(qa_q1ad_ctf *ctf, qa_actor_id actor);
bool qa_q1ad_ctf_attack(qa_q1ad_ctf *ctf, qa_actor_id actor);
void qa_q1ad_ctf_death(qa_q1ad_ctf *ctf, qa_actor_id victim, const qa_actor_id *attacker);
void qa_q1ad_ctf_disconnect_player(qa_q1ad_ctf *ctf, qa_actor_id actor);
void qa_q1ad_ctf_suicide(qa_q1ad_ctf *ctf, qa_actor_id actor);
#endif
