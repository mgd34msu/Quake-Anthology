/* Port of src/content/q1/addons/ctf/arsenal.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_ARSENAL_H
#define QA_CONTENT_Q1_ADDONS_CTF_ARSENAL_H
#include "qa/content/q1/addons/ctf/state.h"
typedef struct qa_q1ad_ctf_character_pose {
  bool axe_pose, has_frame;
  double frame;
} qa_q1ad_ctf_character_pose;
qa_q1ad_ctf_character_pose qa_q1ad_ctf_pose_of(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_spawn_arsenal(qa_q1ad_ctf_state *state, qa_actor_id actor);
bool qa_q1ad_ctf_grapple_attack(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_register_arsenal(qa_q1ad_ctf_state *state);
/* ThreewaveWeapon constructor callbacks + registered rules. */
void qa_q1ad_ctf_weapon_frame(qa_q1ad_ctf_state *state, qa_actor_id actor, double frame);
void qa_q1ad_ctf_weapon_animate(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_rune_before_fire(qa_q1ad_ctf_state *state, qa_q1ad_player *player);
double qa_q1ad_ctf_rune_attack_delay(qa_q1ad_ctf_state *state, qa_q1ad_player *player, double delay);
double qa_q1ad_ctf_rune_nail_speed(qa_q1ad_ctf_state *state, qa_q1ad_player *player, double speed);
double qa_q1ad_ctf_weapon_ammo_grant(qa_q1ad_ctf_state *state, double amount);
bool qa_q1ad_ctf_auto_switch(qa_q1ad_ctf_state *state, qa_q1ad_player *player, bool owned);
double qa_q1ad_ctf_weapon_rank(const char *weapon);
#endif
