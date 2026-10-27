/* Port of src/content/q2/foundation/weapons/hand-grenade.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_HAND_GRENADE_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_HAND_GRENADE_H
#include "qa/content/q2/foundation/weapons/projection.h"
typedef struct qa_q2w_hand_tempo { qa_q2f_edition edition; bool haste, quad_fire; } qa_q2w_hand_tempo;
double qa_q2w_hand_recovery_seconds(qa_q2w_hand_tempo tempo);
double qa_q2w_hand_frame_seconds(qa_q2w_hand_tempo tempo);
double qa_q2w_hand_deadline(double now, double seconds, qa_q2f_edition edition);
double qa_q2w_hand_fuse_deadline(double now, qa_q2f_edition edition);
typedef struct qa_q2w_project_fn {
  void *ctx;
  qa_q2w_projection (*fn)(void *ctx, qa_vec3 angles, qa_vec3 offset);
} qa_q2w_project_fn;
typedef struct qa_q2w_hand_throw_input {
  qa_q2f_edition edition;
  qa_vec3 angles;
  bool alive;
  double now, fuse_deadline, damage_multiplier, gravity;
  bool held;
  qa_q2w_project_fn project;
} qa_q2w_hand_throw_input;
typedef struct qa_q2w_hand_projectile_spec {
  qa_vec3 start, direction;
  double damage, speed, fuse, radius;
  bool held;
  double gravity;
} qa_q2w_hand_projectile_spec;
qa_q2w_hand_projectile_spec qa_q2w_calculate_hand_throw(qa_q2w_hand_throw_input input);
#endif
