/* Port of src/app/bootstrap/simulation/prediction/types.ts (types only).
 * recipe/scene borrowed. relative_command/resolve_controls mirror the
 * sibling slices q3-commands.ts/arsenal-intent.ts (not landed): NULL is
 * identity command / no arsenal rewrite. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_TYPES_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q2/rerelease.h"

typedef struct qa_app_movement_prediction_snapshot {
  double sequence, command_time_milliseconds;
  qa_movement_state state;
  qa_arsenal_state arsenal; /* ammo malloc'd in copies; free via snapshot_destroy */
  qa_actor_animation_state animation;
  qa_movement_environment environment;
  qa_bounds bounds;
  qa_vec3 view_angles;
  double view_height;
  qa_vec3 view_offset;
  bool has_contact;
  qa_trace_hit contact_ground;
  double contact_water_level, contact_water_type;
  bool has_q3_arsenal;
  qa_q3_arsenal_runtime_state q3_arsenal;
} qa_app_movement_prediction_snapshot;

typedef struct qa_app_prediction_command {
  const char *angle_space; /* NULL absent; "absolute"|"source-relative" */
  double sequence, time_milliseconds;
  qa_user_command command;
  bool has_arsenal;
  qa_arsenal_intent arsenal;
} qa_app_prediction_command;

/* TS MovementProbeOptions (no seat). */
typedef struct qa_app_movement_probe_options {
  bool movement_only;
  qa_owned_actor actor;
  const qa_executable_recipe *recipe; /* borrowed, nullable */
  qa_movement_profile profile;
  qa_bounds standing_bounds;
  double standing_view_height;
  qa_scene_queries scene;
  void *host_ctx;
  bool (*is_brush)(void *ctx, qa_trace_hit hit);
  /* Mirror: q3-commands.ts relativeMovementCommand(entry,state).command. */
  qa_user_command (*relative_command)(void *ctx, qa_app_prediction_command entry, qa_movement_state state);
  /* Mirror: arsenal-intent.ts resolveQ3ArsenalControls. */
  qa_q3_arsenal_controls (*resolve_controls)(void *ctx, qa_arsenal_state arsenal,
      const qa_arsenal_intent *intent, qa_user_command command, const char *product);
} qa_app_movement_probe_options;

typedef struct qa_app_movement_prediction_options {
  qa_app_movement_probe_options probe;
  qa_seat_id seat;
} qa_app_movement_prediction_options;

typedef struct qa_app_movement_prediction_result {
  const char *status; /* "predicted"|"unchanged"|"disabled"|"history-exhausted" */
  qa_app_movement_prediction_snapshot player;
  qa_ordered_movement_effect *effects; /* malloc'd, free() */
  size_t effect_count;
} qa_app_movement_prediction_result;

typedef struct qa_app_prediction_step_options {
  qa_scene_queries scene;
  qa_q2_rerelease_movement_context *rerelease_movement; /* borrowed */
  bool has_fixed_milliseconds;
  double fixed_milliseconds;
  bool no_footsteps, gauntlet_hit;
  /* TS `ladders?: boolean`: has_ladders false is undefined (the profile's own setting applies). */
  bool has_ladders, ladders;
  bool has_trace_mask;
  double trace_mask;
  bool first_command;
} qa_app_prediction_step_options;

void qa_app_movement_prediction_snapshot_destroy(qa_app_movement_prediction_snapshot *snapshot);
#endif
