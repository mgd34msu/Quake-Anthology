/* Port of src/content/q3/base/game/missile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MISSILE_H
#define QA_CONTENT_Q3_BASE_GAME_MISSILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/game/ballistics_math.h"
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/projectile.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
typedef struct qa_q3_missile_direction { double x, y, z; } qa_q3_missile_direction;
qa_vec3 qa_q3_snap_vector(qa_vec3 value);
qa_vec3 qa_q3_snap_vector_towards(qa_vec3 value, qa_vec3 toward);
/* TS MissionpackMissileServices; live getters are functions over services ctx. */
typedef struct qa_q3_missionpack_missile_services {
  void *ctx; double (*prox_mine_timeout)(void *ctx); qa_q3_game_random_pick random;
  double (*sound_index)(void *ctx, const char *path);
  qa_q3_reflection_result (*invulnerability_impact)(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point);
} qa_q3_missionpack_missile_services;
/* TS MissileHost. combat is a function so time and cvars stay live across callbacks; previous_time is
 * refreshed by the owner. has_missionpack false is TS null, weapon_behavior NULL is TS undefined. */
typedef struct qa_q3_missile_host {
  void *ctx; qa_q3_base_product product; double previous_time;
  qa_q3_game_combat_context *(*combat)(void *ctx);
  const qa_weapon_behavior_projectile_port *weapon_behavior;
  qa_session_actor_registry *actors; qa_shared_body_table *bodies;
  const qa_q3_base_actor_spatial_queries *spatial;
  qa_q3_base_server_world world;
  bool has_missionpack; qa_q3_missionpack_missile_services missionpack;
} qa_q3_missile_host;
struct qa_q3_native_projectile;
typedef struct qa_q3_missile_runtime {
  qa_q3_missile_host host; bool bound; int64_t release_subscription; int depth;
  struct qa_q3_native_projectile **projectiles; size_t count, capacity;
  struct qa_q3_native_projectile **retired; size_t retired_count, retired_capacity;
  qa_q3_entity_callback proximity_touch;
} qa_q3_missile_runtime;
/* Stores the host; performs the TS constructor work when host.combat is set. */
void qa_q3_missile_runtime_init(qa_q3_missile_runtime *runtime, qa_q3_missile_host host);
/* TS constructor: product check, onRelease subscription, bindSaveCallbacks. Idempotent. */
void qa_q3_missile_runtime_bind(qa_q3_missile_runtime *runtime);
void qa_q3_missile_runtime_close(qa_q3_missile_runtime *runtime);
void qa_q3_missile_bind_save_callbacks(qa_q3_missile_runtime *runtime);
/* TS captureSaveState/restoreSaveState. Capture returns an owned tree, NULL only on OOM. */
qa_save_value *qa_q3_missile_runtime_capture_save_state(qa_q3_missile_runtime *runtime);
typedef qa_actor_id (*qa_q3_missile_resolve_actor_fn)(void *ctx, qa_saved_actor_id saved);
void qa_q3_missile_runtime_restore_save_state(qa_q3_missile_runtime *runtime, const qa_save_value *value, qa_q3_missile_resolve_actor_fn resolve, void *resolve_ctx);
bool qa_q3_missile_runtime_owner_of(qa_q3_missile_runtime *runtime, qa_actor_id actor, qa_actor_id *out); /* false = TS null */
bool qa_q3_missile_runtime_is_proximity_trigger(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity);
bool qa_q3_missile_runtime_run_owned(qa_q3_missile_runtime *runtime, qa_owned_actor actor);
void qa_q3_missile_runtime_bounce(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_server_trace_result trace);
void qa_q3_missile_runtime_explode(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_missile_runtime_impact(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_server_trace_result trace);
void qa_q3_missile_runtime_run(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_missile_runtime_hook_free(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_missile_runtime_hook_think(qa_q3_missile_runtime *runtime, qa_q3_game_entity *entity);
qa_q3_game_entity *qa_q3_missile_runtime_fire_plasma(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *direction);
qa_q3_game_entity *qa_q3_missile_runtime_fire_grenade(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *direction);
qa_q3_game_entity *qa_q3_missile_runtime_fire_rocket(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *direction);
qa_q3_game_entity *qa_q3_missile_runtime_fire_bfg(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *direction);
qa_q3_game_entity *qa_q3_missile_runtime_fire_grapple(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *direction);
qa_q3_game_entity *qa_q3_missile_runtime_fire_prox(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *direction);
qa_q3_game_entity *qa_q3_missile_runtime_fire_nail(qa_q3_missile_runtime *runtime, qa_q3_game_entity *self, qa_vec3 start, qa_vec3 forward, qa_vec3 right, qa_vec3 up);
#endif
