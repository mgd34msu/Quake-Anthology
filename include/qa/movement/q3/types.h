/*
 * Port of src/movement/q3/types.ts: Q3 locomotion work-state, hooks,
 * options and postures. Closures become fn+ctx tables; optional
 * members use has_* flags. `product` keeps exact TS strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_Q3_TYPES_H
#define QA_MOVEMENT_Q3_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"

typedef struct qa_q3_move_command {
  double server_time; qa_vec3 angles; double buttons, weapon;
  double forwardmove, rightmove, upmove;
} qa_q3_move_command;

typedef struct qa_q3_motion {
  double command_time, pm_type, bob_cycle, pm_flags, pm_time;
  qa_vec3 origin, velocity; double gravity, speed; qa_vec3 delta_angles;
  qa_trace_hit ground; double movement_dir; qa_vec3 grapple_point; double e_flags;
  qa_vec3 viewangles; double viewheight, pmove_framecount, event_sequence;
  qa_actor_id actor; double health; bool flight, invulnerable;
  const char *product; /* "baseq3" | "missionpack" */
} qa_q3_motion;

typedef enum qa_q3_animation_request_kind {
  QA_Q3_ANIM_LEGS, QA_Q3_ANIM_LEGS_TIMER, QA_Q3_ANIM_DROP_TIMERS, QA_Q3_ANIM_GESTURE
} qa_q3_animation_request_kind;
typedef struct qa_q3_animation_request {
  qa_q3_animation_request_kind kind;
  double animation; bool force; double milliseconds;
} qa_q3_animation_request;

typedef struct qa_q3_hook_context {
  qa_q3_movement_input input;
  const qa_q3_motion *motion;
  qa_q3_movement_state state;
  qa_q3_move_command command;
  qa_frame_context frame;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  qa_movement_services services;
} qa_q3_hook_context;

typedef struct qa_q3_weapon_phase_result {
  qa_weapon_step_result base;
  double movement_flags;
} qa_q3_weapon_phase_result;

typedef struct qa_q3_movement_hooks {
  void *ctx;
  bool (*firing)(void *ctx, qa_q3_hook_context context);
  qa_animation_step_result (*animation)(void *ctx, qa_q3_animation_request request, qa_q3_hook_context context);
  qa_q3_weapon_phase_result (*weapon)(void *ctx, qa_q3_hook_context context);
  qa_animation_step_result (*torso)(void *ctx, qa_q3_hook_context context);
} qa_q3_movement_hooks;

typedef qa_trace_result (*qa_q3_trace_fn)(void *ctx, qa_vec3 start, qa_vec3 end,
                                          qa_bounds bounds, qa_actor_id pass_actor, double mask);

typedef struct qa_q3_diagnostics {
  void *ctx; double count, level;
  void (*print)(void *ctx, const char *message);
} qa_q3_diagnostics;

typedef struct qa_q3_postures {
  double standing_view_height;
  qa_movement_posture crouched, dead;
  qa_bounds invulnerability_expanded;
} qa_q3_postures;

typedef struct qa_q3_motion_options {
  void *ctx;
  const qa_fixed_movement_pose *pose; /* optional */
  qa_q3_trace_fn trace;
  double (*point_contents)(void *ctx, qa_vec3 point, qa_actor_id pass_actor);
  qa_bounds standing_bounds;
  bool has_current_bounds; qa_bounds current_bounds;
  bool has_body_bounds; const qa_bounds *body_bounds; /* TS getter-backed; read per step */
  qa_q3_postures postures;
  double trace_mask; /* 0 selects TS default (options.traceMask ?? ...) via has flag */
  bool has_trace_mask;
  bool has_fixed_msec; double fixed_msec; /* TS number|null: has=false is null */
  bool no_footsteps;
  bool ladders_disabled; /* TS options.ladders === false */
  /* begin_step/end_step: false return is TS `=== false` break; has_ret distinguishes void */
  bool (*begin_step)(void *ctx, qa_q3_motion *state, qa_q3_move_command *command, double msec, double substep);
  bool (*end_step)(void *ctx, qa_q3_motion *state); /* NULL = absent */
  void (*event)(void *ctx, double event);
  void (*animation)(void *ctx, qa_q3_animation_request request);
  bool (*weapon)(void *ctx); /* returns false only for TS `=== false` */
  void (*torso)(void *ctx);
  bool (*firing)(void *ctx);
  void (*contact)(void *ctx, qa_trace_result trace);
  qa_q3_diagnostics *diagnostics; /* NULL absent */
} qa_q3_motion_options;

typedef struct qa_q3_motion_result {
  qa_trace_result *contacts; size_t contact_count; /* malloc'd, caller frees */
  qa_bounds bounds; double waterlevel, watertype, xyspeed;
} qa_q3_motion_result;

#endif
