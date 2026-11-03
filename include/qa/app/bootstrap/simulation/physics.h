/* Port of src/app/bootstrap/simulation/physics.ts. Shared body
 * physics adapted from Quake sv_phys.c/sv_move.c and Quake II
 * g_phys.c/m_move.c. Copyright (C) 1996-2005 Id Software.
 * The scheduler calls one actor at a time; this owner never starts a
 * family loop. Step/push/walk/fly logic runs in this module over the
 * landed swept-body, Q1 pusher, new-toss and rerelease slide modules.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PHYSICS_H
#define QA_APP_BOOTSTRAP_SIMULATION_PHYSICS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/world.h"
#include "qa/movement/q1/pusher.h"
#include "qa/movement/q2/rerelease.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
typedef const char *qa_app_sim_physics_family; /* "q1"|"q2"|"q3" */
typedef struct qa_app_sim_shared_solid {
  const char *solid; bool has_model; double model;
  qa_app_sim_physics_family family;
  const qa_actor_id *owner;
  bool has_monster; bool monster; bool has_dead_monster; bool dead_monster;
  bool q1_corpse; bool has_item; bool item;
} qa_app_sim_shared_solid;
/* TS optional flag members: has_* marks a present member (partial
 * updates via set_flags merge only present members). */
typedef struct qa_app_sim_shared_physics_flags {
  bool has_always_touch; bool always_touch;
  bool has_fly; bool fly;
  bool has_swim; bool swim;
  bool has_partial_ground; bool partial_ground;
  bool has_dead; bool dead;
  bool has_player; bool player;
  bool has_water_level; double water_level;
  bool has_water_type; double water_type;
  bool has_enemy; const qa_actor_id *enemy;
  bool has_delta_yaw; double delta_yaw;
  bool has_team_slave; bool team_slave;
  /* JS key insertion order of the present members (spread merges keep the
   * first position of a key). key_count 0 means the checkpoint reader's
   * literal order (qa_app_sim_physics_flag_key). */
  unsigned char key_order[11]; unsigned char key_count;
} qa_app_sim_shared_physics_flags;
/* Member keys, in the SharedPhysics checkpoint reader's literal order. */
typedef enum qa_app_sim_physics_flag_key {
  QA_APP_SIM_FLAG_TEAM_SLAVE, QA_APP_SIM_FLAG_ALWAYS_TOUCH, QA_APP_SIM_FLAG_FLY, QA_APP_SIM_FLAG_SWIM,
  QA_APP_SIM_FLAG_PARTIAL_GROUND, QA_APP_SIM_FLAG_DEAD, QA_APP_SIM_FLAG_PLAYER, QA_APP_SIM_FLAG_WATER_LEVEL,
  QA_APP_SIM_FLAG_WATER_TYPE, QA_APP_SIM_FLAG_DELTA_YAW, QA_APP_SIM_FLAG_ENEMY, QA_APP_SIM_FLAG_COUNT
} qa_app_sim_physics_flag_key;
bool qa_app_sim_physics_flags_has(const qa_app_sim_shared_physics_flags *flags, qa_app_sim_physics_flag_key key);
/* Effective key order of the present members; returns the count. */
size_t qa_app_sim_physics_flags_keys(const qa_app_sim_shared_physics_flags *flags, unsigned char out[QA_APP_SIM_FLAG_COUNT]);
/* Records the present members' insertion order as `keys` (a literal's
 * order); members missing from `keys` keep reader order after them. */
void qa_app_sim_physics_flags_order(qa_app_sim_shared_physics_flags *flags, const unsigned char *keys, size_t count);
/* `{ ...dst, ...src }`. */
void qa_app_sim_physics_flags_merge(qa_app_sim_shared_physics_flags *dst, const qa_app_sim_shared_physics_flags *src);
typedef struct qa_app_sim_source_body_physics {
  void *ctx;
  bool (*collision)(void *ctx, qa_app_sim_shared_solid *out);
  bool (*motion)(void *ctx, qa_q2_motion *out);
  qa_app_sim_shared_physics_flags (*flags)(void *ctx);
  void (*write_flags)(void *ctx, qa_app_sim_shared_physics_flags changes);
  void (*write_angular_velocity)(void *ctx, qa_vec3 value);
  void (*water_transition)(void *ctx);
} qa_app_sim_source_body_physics;
typedef struct qa_app_sim_physics_event { qa_actor_id actor; const char *kind; qa_vec3 origin; } qa_app_sim_physics_event;
typedef struct qa_app_sim_shared_physics_options {
  void *ctx;
  void (*q1_water_transition)(void *ctx, qa_owned_actor actor); /* NULL absent */
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_scene_queries *scene;
  qa_numeric_profile numeric;
  double (*source_order)(void *ctx, qa_actor_id a, qa_actor_id b);
  bool (*world_actor)(void *ctx, qa_actor_id *out);
  void (*on_blocked)(void *ctx, qa_owned_actor pusher, qa_actor_id other);
  bool (*get_collision)(void *ctx, qa_owned_actor actor, qa_app_sim_shared_solid *out);
  bool (*get_motion)(void *ctx, qa_owned_actor actor, qa_q2_motion *out);
  bool (*get_flags)(void *ctx, qa_owned_actor actor, qa_app_sim_shared_physics_flags *out);
  void (*write_flags)(void *ctx, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes);
  void (*write_angular_velocity)(void *ctx, qa_owned_actor actor, qa_vec3 velocity);
  void (*event)(void *ctx, qa_app_sim_physics_event event);
  bool has_gravity; double gravity;
  bool has_max_velocity; double max_velocity;
  const char *q2_edition; /* "classic"|"rerelease" */
  double (*stop_speed)(void *ctx);
  bool (*take_kill_velocity)(void *ctx, qa_owned_actor actor);
  /* Legacy host-bundle hooks from the shell port; the TS logic now
   * runs in-module, so these are no longer consulted. Kept so
   * existing option initializers keep compiling. */
  void (*step_move)(void *ctx, qa_owned_actor actor, double elapsed, qa_q2_motion motion);
  void (*push_move)(void *ctx, qa_owned_actor actor, double elapsed, qa_q2_motion motion);
} qa_app_sim_shared_physics_options;
typedef struct qa_app_sim_shared_physics qa_app_sim_shared_physics;
typedef struct qa_app_sim_physics_collision_entry { qa_saved_actor_id actor; qa_actor_collision collision; } qa_app_sim_physics_collision_entry;
typedef struct qa_app_sim_physics_solid_entry { qa_saved_actor_id actor; qa_app_sim_shared_solid solid; qa_saved_actor_id owner; bool has_owner; } qa_app_sim_physics_solid_entry;
typedef struct qa_app_sim_physics_motion_entry { qa_saved_actor_id actor; qa_q2_motion motion; qa_saved_actor_id owner; bool has_owner; } qa_app_sim_physics_motion_entry;
typedef struct qa_app_sim_physics_flags_entry { qa_saved_actor_id actor; qa_app_sim_shared_physics_flags flags; qa_saved_actor_id enemy; } qa_app_sim_physics_flags_entry;
typedef struct qa_app_sim_physics_capture {
  double gravity; qa_vec3 rerelease_movement;
  qa_app_sim_physics_collision_entry *collisions; size_t collision_count;
  qa_app_sim_physics_collision_entry *spatial; size_t spatial_count;
  qa_app_sim_physics_solid_entry *solids; size_t solid_count;
  qa_app_sim_physics_motion_entry *motions; size_t motion_count;
  qa_app_sim_physics_flags_entry *flags; size_t flag_count;
} qa_app_sim_physics_capture;
qa_app_sim_shared_physics *qa_app_sim_shared_physics_create(qa_app_sim_shared_physics_options options);
void qa_app_sim_shared_physics_destroy(qa_app_sim_shared_physics *physics);
qa_shared_body_table *qa_app_sim_shared_physics_bodies(qa_app_sim_shared_physics *physics);
qa_q2_rerelease_movement_context *qa_app_sim_shared_physics_rerelease_movement(qa_app_sim_shared_physics *physics);
/* Returns an unbind handle for qa_app_sim_shared_physics_unbind_source. */
int qa_app_sim_shared_physics_bind_source(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_source_body_physics source);
void qa_app_sim_shared_physics_unbind_source(qa_app_sim_shared_physics *physics, int handle);
double qa_app_sim_shared_physics_gravity(qa_app_sim_shared_physics *physics);
void qa_app_sim_shared_physics_set_world_gravity(qa_app_sim_shared_physics *physics, double value);
void qa_app_sim_shared_physics_set_solid(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_shared_solid solid);
void qa_app_sim_shared_physics_set_collision(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_actor_collision collision);
void qa_app_sim_shared_physics_set_motion(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_q2_motion motion);
void qa_app_sim_shared_physics_set_flags(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_shared_physics_flags flags);
bool qa_app_sim_shared_physics_collision_of(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_actor_collision *out);
/* TS solidOf/motionOf/isBrush by actor id. */
bool qa_app_sim_shared_physics_solid_of_actor(qa_app_sim_shared_physics *physics, qa_actor_id actor, qa_app_sim_shared_solid *out);
bool qa_app_sim_shared_physics_motion_of_actor(qa_app_sim_shared_physics *physics, qa_actor_id actor, qa_q2_motion *out);
bool qa_app_sim_shared_physics_is_brush(qa_app_sim_shared_physics *physics, qa_actor_id actor);
/* TS drainEvents: returned array is physics-owned, valid until the
 * next event is emitted. */
const qa_app_sim_physics_event *qa_app_sim_shared_physics_drain_events(qa_app_sim_shared_physics *physics, size_t *out_count);
qa_trace_result qa_app_sim_shared_physics_trace(qa_app_sim_shared_physics *physics, qa_q2_trace_request request, qa_app_sim_physics_family family);
/* TS step: returns "moved"|"stopped"|"team-slave"|"removed" for
 * new-toss, NULL (TS undefined) otherwise. */
const char *qa_app_sim_shared_physics_step(qa_app_sim_shared_physics *physics, qa_owned_actor actor, double elapsed);
/* TS pushMove/pushTeam: false is the TS null; true sets *out_blocked. */
bool qa_app_sim_shared_physics_push_move(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_vec3 displacement, qa_vec3 angular_displacement, qa_actor_id *out_blocked);
bool qa_app_sim_shared_physics_push_team(qa_app_sim_shared_physics *physics, const qa_owned_actor *actors, size_t actor_count, double elapsed, qa_actor_id *out_blocked);
bool qa_app_sim_shared_physics_walk_move(qa_app_sim_shared_physics *physics, qa_owned_actor actor, double yaw, double distance);
void qa_app_sim_shared_physics_water_transition(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_vec3 previous_origin);
void qa_app_sim_shared_physics_touch_triggers(qa_app_sim_shared_physics *physics, qa_owned_actor actor);
void qa_app_sim_shared_physics_commit_attachments(qa_app_sim_shared_physics *physics);
/* TS q1PusherServices/readQ1Pusher/writeQ1Pusher: ctx must outlive use.
 * The env carries the caller projection (read/write/link/blocked). */
typedef struct qa_app_sim_q1_pusher_env {
  qa_app_sim_shared_physics *physics;
  void *projection_ctx;
  bool (*read)(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out);
  void (*write)(void *ctx, qa_q1_physics_entity entity);
  void (*link)(void *ctx, qa_owned_actor actor, bool touch_triggers);
  void (*blocked)(void *ctx, qa_owned_actor pusher, qa_actor_id obstacle);
  qa_actor_id *candidates; size_t candidate_count;
} qa_app_sim_q1_pusher_env;
qa_q1_pusher_services qa_app_sim_shared_physics_q1_pusher_services(qa_app_sim_q1_pusher_env *env);
bool qa_app_sim_shared_physics_read_q1_pusher(qa_app_sim_shared_physics *physics, qa_actor_id actor, qa_q1_physics_entity *out);
void qa_app_sim_shared_physics_write_q1_pusher(qa_app_sim_shared_physics *physics, qa_q1_physics_entity entity);
qa_app_sim_physics_capture qa_app_sim_shared_physics_capture(qa_app_sim_shared_physics *physics);
void qa_app_sim_physics_capture_free(qa_app_sim_physics_capture *capture);
void qa_app_sim_shared_physics_restore_checkpoint(qa_app_sim_shared_physics *physics, const qa_save_reader *reader,
    bool require_exact_collisions, bool (*reconstructed)(void *ctx, qa_actor_id actor), void *reconstructed_ctx);
void qa_app_sim_shared_physics_restore_spatial(qa_app_sim_shared_physics *physics, const qa_save_reader *reader,
    bool (*reconstructed)(void *ctx, qa_actor_id actor), void *reconstructed_ctx);
#endif
