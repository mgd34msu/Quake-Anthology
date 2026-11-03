/* Port of src/content/q2/foundation/weapons/hand-action.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_HAND_ACTION_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_HAND_ACTION_H
#include "qa/content/q2/foundation/weapons/hand_grenade.h"
typedef enum qa_q2w_hand_action_kind {
  QA_Q2W_HA_IDLE, QA_Q2W_HA_DISARMED, QA_Q2W_HA_PREPARING,
  QA_Q2W_HA_COOKING, QA_Q2W_HA_RELEASING, QA_Q2W_HA_RECOVERING
} qa_q2w_hand_action_kind;
typedef struct qa_q2w_hand_action {
  qa_q2w_hand_action_kind kind;
  double frame, next_at; bool release_queued; /* preparing */
  double expires_at;                            /* cooking/releasing */
  double throw_at;                              /* releasing */
  double ready_at; bool require_release;        /* recovering */
} qa_q2w_hand_action;
typedef enum qa_q2w_lifecycle { QA_Q2W_ALIVE, QA_Q2W_DEAD, QA_Q2W_REMOVING, QA_Q2W_REMOVED } qa_q2w_lifecycle;
typedef struct qa_q2w_hand_action_input {
  qa_q2_edition edition; bool haste, quad_fire;
  double now;
  bool pressed, held, released;
  qa_q2w_lifecycle lifecycle;
  bool enabled;
  qa_vec3 throw_angles;
  double throw_damage_multiplier, throw_gravity;
  qa_q2w_project_fn throw_project;
} qa_q2w_hand_action_input;
typedef struct qa_q2w_hand_action_host {
  void *ctx;
  double (*firing_interval)(void *ctx, double seconds); /* optional */
  bool (*reserve)(void *ctx);
  void (*consume)(void *ctx);
  void (*refund)(void *ctx);
  void (*emit)(void *ctx, qa_q2w_hand_projectile_spec spec);
  void (*sound)(void *ctx, const char *event); /* "cock"|"cook-start"|"cook-stop" */
} qa_q2w_hand_action_host;
qa_q2w_hand_action qa_q2w_step_hand_action(qa_q2w_hand_action state, qa_q2w_hand_action_input input,
                                           const qa_q2w_hand_action_host *host);
#endif
