/*
 * Port of src/movement/q1/types.ts: Quake movement constants and
 * interfaces derived from WinQuake/sv_phys.c and QW/pmove.h.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later.
 * C accommodations: optional hook members are nullable function
 * pointers; optional option fields use has_* flags or NULL strings
 * (a zero-initialized options struct is the TS {}). String unions
 * (solid, jumpAuthority, fixAngleRoll) keep their exact TS strings.
 */
#ifndef QA_MOVEMENT_Q1_TYPES_H
#define QA_MOVEMENT_Q1_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"

#define QA_Q1_MOVE_NONE 0
#define QA_Q1_MOVE_WALK 3
#define QA_Q1_MOVE_STEP 4
#define QA_Q1_MOVE_FLY 5
#define QA_Q1_MOVE_TOSS 6
#define QA_Q1_MOVE_PUSH 7
#define QA_Q1_MOVE_NOCLIP 8
#define QA_Q1_MOVE_FLYMISSILE 9
#define QA_Q1_MOVE_BOUNCE 10
#define QA_Q1_MOVE_GIB 11
#define QA_Q1_FLAG_FLY 1
#define QA_Q1_FLAG_SWIM 2
#define QA_Q1_FLAG_ONGROUND 512
#define QA_Q1_FLAG_WATERJUMP 2048
#define QA_Q1_FLAG_JUMPRELEASED 4096
#define QA_Q1_CONTENTS_EMPTY (-1)
#define QA_Q1_CONTENTS_SOLID (-2)
#define QA_Q1_CONTENTS_WATER (-3)
#define QA_Q1_CONTENTS_SLIME (-4)
#define QA_Q1_CONTENTS_LAVA (-5)
#define QA_Q1_STEP_HEIGHT 18

/** TS Q1PlayerInput (q1-netquake | q1-quakeworld movement input). */
typedef qa_movement_input qa_q1_player_input;

/** TS Q1MovementHooks. link/is_bsp/before_physics/after_physics are
 * required when hooks are present; the rest may be NULL. */
typedef struct qa_q1_movement_hooks {
  void *ctx;
  void (*qw_state)(void *ctx, double water_level, double water_type);
  qa_trace_shape (*shape)(void *ctx);
  void (*body_shape)(void *ctx, qa_bounds bounds);
  qa_movement_continuation (*link)(void *ctx, qa_owned_actor actor, qa_movement_state state, bool touch_triggers);
  bool (*is_bsp)(void *ctx, qa_trace_hit hit);
  qa_movement_continuation (*before_physics)(void *ctx, qa_q1_player_input input, qa_movement_state state);
  qa_movement_continuation (*think)(void *ctx, qa_q1_player_input input, qa_movement_state state);
  qa_movement_continuation (*after_physics)(void *ctx, qa_q1_player_input input, qa_movement_state state);
  /* path: "misc/h2ohit1.wav" | "demon/dland2.wav". */
  void (*sound)(void *ctx, qa_owned_actor actor, const char *path, qa_q1_movement_state state);
  /* action: "jump" | "swim". */
  void (*player_action)(void *ctx, qa_owned_actor actor, const char *action, qa_q1_movement_state state);
} qa_q1_movement_hooks;

/** TS Q1MovementOptions. Zero is the TS {} (all fields absent). */
typedef struct qa_q1_movement_options {
  bool has_source_punch_angles;
  qa_vec3 source_punch_angles;
  bool has_view_height;
  double view_height;
  bool has_max_velocity;
  double max_velocity;
  bool no_step;
  bool has_ideal_pitch_scale;
  double ideal_pitch_scale;
  bool has_roll_speed;
  double roll_speed;
  bool has_roll_angle;
  double roll_angle;
  /* "not" | "trigger" | "box" | "slidebox" | "bsp" | "corpse"; NULL absent. */
  const char *solid;
  /* "selected-movement" | "source-gamecode"; NULL = selected-movement. */
  const char *jump_authority;
  /* "source" | "preserve"; NULL = "source". */
  const char *fix_angle_roll;
  /* QuakeC WaterMove's `velocity -= 0.8 * waterlevel * frametime * velocity`, for sessions
   * whose player prethink is not QuakeC bytecode (TS sourceWaterDrag === true). */
  bool source_water_drag;
  const qa_q1_movement_hooks *hooks; /* NULL absent */
} qa_q1_movement_options;

/** TS Q1PhysicsEntity. */
typedef struct qa_q1_physics_entity {
  qa_owned_actor actor;
  qa_q1_movement_state state;
  qa_bounds bounds;
  qa_bounds absolute_bounds;
  const char *solid; /* exact TS union string */
  double local_time_seconds;
  double next_think_seconds;
} qa_q1_physics_entity;

/** TS Q1PusherServices.push result. has_entity false is the TS null. */
typedef struct qa_q1_push_result {
  bool has_entity;
  qa_q1_physics_entity entity;
  qa_trace_result trace;
} qa_q1_push_result;

/** TS Q1PusherServices. read returns false for the TS null. */
typedef struct qa_q1_pusher_services {
  void *ctx;
  qa_numeric_operations numeric; /* TS movement: Pick<MovementServices, "numeric"> */
  bool (*read)(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out);
  const qa_actor_id *(*candidates)(void *ctx, size_t *out_count);
  void (*write)(void *ctx, qa_q1_physics_entity entity);
  void (*link)(void *ctx, qa_owned_actor actor, bool touch_triggers);
  void (*collision_enabled)(void *ctx, qa_owned_actor actor, bool enabled);
  qa_trace_hit (*test_position)(void *ctx, qa_q1_physics_entity entity);
  qa_q1_push_result (*push)(void *ctx, qa_q1_physics_entity entity, qa_vec3 displacement);
  void (*blocked)(void *ctx, qa_owned_actor pusher, qa_actor_id obstacle);
  void (*think)(void *ctx, qa_owned_actor pusher); /* step_q1_pusher only */
} qa_q1_pusher_services;

/** TS Q1PusherResult. moved is malloc'd; the caller frees it. */
typedef struct qa_q1_pusher_result {
  qa_actor_id actor;
  const char *status; /* "moved" | "blocked" | "actor-removed" */
  qa_actor_id *moved;
  size_t moved_count;
} qa_q1_pusher_result;

#endif
