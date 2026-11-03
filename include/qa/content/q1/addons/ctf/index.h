/* Port of src/content/q1/addons/ctf/index.ts (Q1Ctf extends CtfState: the
 * same struct). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_INDEX_H
#define QA_CONTENT_Q1_ADDONS_CTF_INDEX_H
#include "qa/content/q1/addons/ctf/arsenal.h"
#include "qa/content/q1/addons/ctf/state.h"
typedef qa_q1ad_ctf_state qa_q1ad_ctf;
/* registerCTF(context, services, sharedGrapple = null): heap object the game
 * borrows (named callbacks, spawns, weapon/rules/extension registrations).
 * NULL only on OOM. Destroy after the last use of the game, before the actor
 * registry is destroyed. */
qa_q1ad_ctf *qa_q1ad_register_ctf(qa_q1ad_context *context, qa_q1ad_ctf_services services,
                                  const qa_shared_grapple_control *shared);
void qa_q1ad_ctf_destroy(qa_q1ad_ctf *ctf);
void qa_q1ad_ctf_spawn_player(qa_q1ad_ctf *ctf, qa_actor_id actor, bool first_admission);
qa_q1_actor *qa_q1ad_ctf_select_spawn(qa_q1ad_ctf *ctf, qa_actor_id actor);
qa_q1ad_ctf_character_pose qa_q1ad_ctf_player_pose(qa_q1ad_ctf *ctf, qa_actor_id actor);
bool qa_q1ad_ctf_fall_damage_allowed(qa_q1ad_ctf *ctf, qa_actor_id actor);
uint8_t *qa_q1ad_ctf_capture_player_travel(qa_q1ad_ctf *ctf, qa_actor_id actor, size_t *out_length);
void qa_q1ad_ctf_restore_player_travel(qa_q1ad_ctf *ctf, qa_actor_id actor, const uint8_t *bytes, size_t length);
void qa_q1ad_ctf_player_frame(qa_q1ad_ctf *ctf, qa_actor_id actor);
void qa_q1ad_ctf_after_physics(qa_q1ad_ctf *ctf, qa_actor_id actor);
bool qa_q1ad_ctf_impulse(qa_q1ad_ctf *ctf, qa_actor_id actor);
bool qa_q1ad_ctf_attack(qa_q1ad_ctf *ctf, qa_actor_id actor);
/* attacker NULL = TS null. */
void qa_q1ad_ctf_death(qa_q1ad_ctf *ctf, qa_actor_id victim, const qa_actor_id *attacker);
void qa_q1ad_ctf_disconnect_player(qa_q1ad_ctf *ctf, qa_actor_id actor);
void qa_q1ad_ctf_suicide(qa_q1ad_ctf *ctf, qa_actor_id actor);
#endif
