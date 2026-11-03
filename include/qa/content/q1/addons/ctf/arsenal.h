/* Port of src/content/q1/addons/ctf/arsenal.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_ARSENAL_H
#define QA_CONTENT_Q1_ADDONS_CTF_ARSENAL_H
#include "qa/content/q1/addons/ctf/state.h"
/* TS CtfCharacterPose; has_frame false = TS null frame. */
typedef struct qa_q1ad_ctf_character_pose {
  bool axe_pose, has_frame;
  double frame;
} qa_q1ad_ctf_character_pose;
qa_q1ad_ctf_character_pose qa_q1ad_ctf_character_pose_of(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_spawn_arsenal(qa_q1ad_ctf_state *state, qa_actor_id actor);
bool qa_q1ad_ctf_grapple_attack(qa_q1ad_ctf_state *state, qa_actor_id actor);
/* False only on OOM. */
bool qa_q1ad_ctf_register_arsenal(qa_q1ad_ctf_state *state);
#endif
