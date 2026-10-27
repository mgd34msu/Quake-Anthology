/*
 * Port of src/movement/q1/monsters.ts: WinQuake/sv_move.c and
 * PF_changeyaw/PF_walkmove from pr_cmds.c.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later.
 * C accommodation: TS nullable reads return false; the class becomes
 * an init-in-place struct (qa_q1_monster_movement_init is the TS
 * createQ1MonsterMovement factory).
 */
#ifndef QA_MOVEMENT_Q1_MONSTERS_H
#define QA_MOVEMENT_Q1_MONSTERS_H

#include <stdbool.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q1/common.h"

#define QA_Q1_FLAG_PARTIALGROUND 1024

/** TS Q1MonsterMoveState. enemy is owned iff has_enemy. */
typedef struct qa_q1_monster_move_state {
  qa_vec3 origin;
  qa_vec3 angles;
  qa_bounds bounds;
  qa_bounds absolute_bounds;
  double flags;
  qa_trace_hit ground;
  double ideal_yaw;
  double yaw_speed;
  bool has_enemy;
  qa_actor_id enemy;
} qa_q1_monster_move_state;

/** TS readTarget result: Pick<Q1MonsterMoveState, "origin" | "absoluteBounds">. */
typedef struct qa_q1_monster_target {
  qa_vec3 origin;
  qa_bounds absolute_bounds;
} qa_q1_monster_target;

/** TS Pick<RandomSource, "nextInteger">. */
typedef struct qa_q1_monster_random {
  void *ctx;
  double (*next_integer)(void *ctx);
} qa_q1_monster_random;

/** TS Q1MonsterMoveServices. read/read_target false is the TS null. */
typedef struct qa_q1_monster_move_services {
  void *ctx;
  qa_scene_queries scene; /* only trace/point_contents are called */
  qa_numeric_operations numeric;
  qa_q1_monster_random random;
  bool (*read)(void *ctx, qa_actor_id actor, qa_q1_monster_move_state *out);
  bool (*read_target)(void *ctx, qa_actor_id actor, qa_q1_monster_target *out);
  void (*write)(void *ctx, qa_owned_actor actor, qa_q1_monster_move_state state);
  void (*link)(void *ctx, qa_owned_actor actor, bool touch_triggers);
} qa_q1_monster_move_services;

/** TS Q1MonsterMovement. Movement builtins share the simulation's
 * actor, collision, and random owners. */
typedef struct qa_q1_monster_movement {
  qa_q1_monster_move_services services;
  qa_q1_movement_math math;
} qa_q1_monster_movement;

void qa_q1_monster_movement_init(qa_q1_monster_movement *movement, qa_q1_monster_move_services services);
void qa_q1_monster_change_yaw(qa_q1_monster_movement *movement, qa_owned_actor actor);
bool qa_q1_monster_check_bottom(qa_q1_monster_movement *movement, qa_actor_id actor);
bool qa_q1_monster_move_step(qa_q1_monster_movement *movement, qa_owned_actor actor, qa_vec3 move, bool relink);
bool qa_q1_monster_step_direction(qa_q1_monster_movement *movement, qa_owned_actor actor, double yaw, double distance);
bool qa_q1_monster_walk_move(qa_q1_monster_movement *movement, qa_owned_actor actor, double yaw, double distance);
bool qa_q1_monster_close_enough(qa_q1_monster_movement *movement, qa_actor_id actor, qa_actor_id goal, double distance);
void qa_q1_monster_new_chase_direction(qa_q1_monster_movement *movement, qa_owned_actor actor, qa_actor_id goal,
                                       double distance);
/* mode: "range" | "contact"; NULL is the TS default "range". */
void qa_q1_monster_move_to_goal(qa_q1_monster_movement *movement, qa_owned_actor actor, qa_actor_id goal,
                                double distance, const char *mode);

#endif
