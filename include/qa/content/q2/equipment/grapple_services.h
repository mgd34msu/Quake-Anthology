/* Port of src/content/q2/equipment/grapple-services.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_EQUIPMENT_GRAPPLE_SERVICES_H
#define QA_CONTENT_Q2_EQUIPMENT_GRAPPLE_SERVICES_H
#include "qa/content/q2/foundation/host.h"
typedef struct qa_grapple_pose { qa_vec3 angles; const char *hand; double view_height, gravity; qa_vec3 gravity_vector; } qa_grapple_pose;
/* TS GrappleAnchor / cable event. */
typedef struct qa_grapple_cable_event { const char *kind; qa_actor_id actor; qa_vec3 start, end, offset; } qa_grapple_cable_event;
typedef struct qa_grapple_hooks {
  void *ctx;
  qa_grapple_pose (*pose)(void *ctx, qa_actor_id a, qa_q2_game_services *g);
  const char *(*anchor)(void *ctx, qa_actor_id a, qa_q2_game_services *g);
  bool (*dead)(void *ctx, qa_actor_id a, qa_q2_game_services *g);
  qa_vec3 (*previous_velocity)(void *ctx, qa_actor_id a);
  void (*set_previous_velocity)(void *ctx, qa_actor_id a, qa_vec3 v);
  double (*volume)(void *ctx, qa_actor_id a);
  void (*noise)(void *ctx, qa_actor_id a, qa_q2_game_services *g, qa_vec3 origin, const char *kind);
  void (*set_grapple_prediction)(void *ctx, qa_actor_id a, bool suppressed);
  double (*gravity)(void *ctx);
  void (*emit)(void *ctx, qa_grapple_cable_event event);
} qa_grapple_hooks;
qa_body_state qa_grapple_body(qa_actor_id a, qa_q2_game_services *g);
void qa_grapple_velocity(qa_actor_id a, qa_q2_game_services *g, qa_vec3 v);
typedef struct qa_ctf_grapple_state { bool has_grapple; qa_actor_id grapple; const char *grapple_state; double grapple_release_time; bool has_no_knockback; bool grapple_no_knockback; } qa_ctf_grapple_state;
typedef struct qa_lmctf_grapple_state { bool has_hook; qa_actor_id hook; double hook_state, hook_length; bool hook_held; } qa_lmctf_grapple_state;
void qa_ctf_grapple_state_init(qa_ctf_grapple_state *s);
void qa_lmctf_grapple_state_init(qa_lmctf_grapple_state *s);
typedef struct qa_ctf_grapple_checkpoint { bool has_grapple; qa_saved_actor_id grapple; const char *grapple_state; double grapple_release_time; bool has_no_knockback; bool grapple_no_knockback; } qa_ctf_grapple_checkpoint;
typedef struct qa_lmctf_grapple_checkpoint { bool has_hook; qa_saved_actor_id hook; double hook_state, hook_length; bool hook_held; } qa_lmctf_grapple_checkpoint;
qa_ctf_grapple_checkpoint qa_capture_ctf_grapple(qa_ctf_grapple_state s);
qa_lmctf_grapple_checkpoint qa_capture_lmctf_grapple(qa_lmctf_grapple_state s);
qa_ctf_grapple_state qa_restore_ctf_grapple(qa_ctf_grapple_checkpoint s, qa_q2_game_services *g);
qa_lmctf_grapple_state qa_restore_lmctf_grapple(qa_lmctf_grapple_checkpoint s, qa_q2_game_services *g);
#endif
