/* Port of src/content/q1/equipment/threewave-grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_EQUIPMENT_THREEWAVE_GRAPPLE_H
#define QA_CONTENT_Q1_EQUIPMENT_THREEWAVE_GRAPPLE_H

#include "qa/content/q1/foundation/entity_services.h"

typedef struct qa_q1_threewave_input {
  bool held, release, jump;
  qa_vec3 view_angles;
  double teleport_until;
} qa_q1_threewave_input;
typedef struct qa_q1_threewave_anchor { bool solid, centered, player; } qa_q1_threewave_anchor;
typedef struct qa_q1_threewave_host {
  void *ctx;
  qa_q1_threewave_input (*input)(void *ctx, qa_actor_id actor);
  qa_vec3 (*aim)(void *ctx, qa_actor_id actor, qa_vec3 forward);
  qa_q1_threewave_anchor (*anchor)(void *ctx, qa_actor_id actor);
  bool (*can_attach)(void *ctx, qa_actor_id owner, qa_actor_id target);
  bool (*can_pulse)(void *ctx, qa_actor_id owner, qa_actor_id target);
  bool (*can_damage)(void *ctx, qa_actor_id target, qa_actor_id owner);
} qa_q1_threewave_host;

typedef struct qa_q1_threewave_state {
  bool pulling;
  double weapon_frame, attack_finished, release_time;
  bool has_animation; qa_actor_id animation;
} qa_q1_threewave_state;

typedef struct qa_q1_threewave_grapple {
  qa_q1_services *game;
  qa_q1_threewave_host host;
  struct { qa_actor_id actor; qa_q1_threewave_state state; } *states; size_t state_count, state_capacity;
} qa_q1_threewave_grapple;

/* Registers ctf:hook_* callbacks + state extension. False only on OOM. */
bool qa_q1_threewave_grapple_init(qa_q1_threewave_grapple *grapple, qa_q1_services *game, qa_q1_threewave_host host);
void qa_q1_threewave_grapple_free(qa_q1_threewave_grapple *grapple);
/* TS ThreewaveGrapple.state(); suffixed because the state type
 * carries the qa_q1_threewave_state name. */
qa_q1_threewave_state *qa_q1_threewave_state_for(qa_q1_threewave_grapple *grapple, qa_actor_id actor);
bool qa_q1_threewave_pulling(qa_q1_threewave_grapple *grapple, qa_actor_id actor);
qa_q1_actor *qa_q1_threewave_hook(qa_q1_threewave_grapple *grapple, qa_actor_id actor);
void qa_q1_threewave_release(qa_q1_threewave_grapple *grapple, qa_actor_id actor);
void qa_q1_threewave_touch(qa_q1_threewave_grapple *grapple, qa_q1_actor *hook, qa_actor_id other, const qa_touch_surface *surface);
bool qa_q1_threewave_fire(qa_q1_threewave_grapple *grapple, qa_actor_id actor);
void qa_q1_threewave_trail(qa_q1_threewave_grapple *grapple, qa_actor_id actor);
/* Actor-release housekeeping (TS host.actors.onRelease hook); call on release. */
void qa_q1_threewave_on_release(qa_q1_threewave_grapple *grapple, qa_actor_id actor);

#endif
