/* Port of src/content/q1/equipment/threewave-weapon.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_EQUIPMENT_THREEWAVE_WEAPON_H
#define QA_CONTENT_Q1_EQUIPMENT_THREEWAVE_WEAPON_H

#include "qa/content/q1/equipment/threewave_grapple.h"

typedef struct qa_q1_threewave_weapon_hooks {
  void *ctx;
  bool (*selected)(void *ctx, qa_actor_id actor);
  bool (*available)(void *ctx, qa_actor_id actor);
  void (*frame)(void *ctx, qa_actor_id actor, double frame);
} qa_q1_threewave_weapon_hooks;

typedef struct qa_q1_threewave_weapon {
  qa_q1_threewave_grapple *core;
  qa_q1_threewave_weapon_hooks hooks;
} qa_q1_threewave_weapon;

void qa_q1_threewave_weapon_init(qa_q1_threewave_weapon *weapon, qa_q1_threewave_grapple *core, qa_q1_threewave_weapon_hooks hooks);
bool qa_q1_threewave_weapon_attack(qa_q1_threewave_weapon *weapon, qa_actor_id actor);
void qa_q1_threewave_weapon_animate(qa_q1_threewave_weapon *weapon, qa_actor_id actor);
void qa_q1_threewave_weapon_holster(qa_q1_threewave_weapon *weapon, qa_actor_id actor);
bool qa_q1_threewave_weapon_is_holstered(qa_q1_threewave_weapon *weapon);
void qa_q1_threewave_weapon_resume(qa_q1_threewave_weapon *weapon, qa_actor_id actor);
/* TS threewaveCharacterPose; out_frame false = TS null frame. */
void qa_q1_threewave_character_pose(qa_q1_threewave_grapple *core, qa_actor_id actor, bool *out_axe_pose, bool *out_has_frame, double *out_frame);

#endif
