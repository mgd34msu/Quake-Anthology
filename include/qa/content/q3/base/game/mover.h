/* Port of src/content/q3/base/game/mover.ts. GPL-2.0-or-later.
 * Pose writes go through entity.r, which forwards to the shared body. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MOVER_H
#define QA_CONTENT_Q3_BASE_GAME_MOVER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/world.h"
#include "qa/world/actors/body.h"
typedef enum qa_q3_shared_mover_kind { QA_Q3_MOVER_BODY_PLAYER, QA_Q3_MOVER_BODY_MOVABLE, QA_Q3_MOVER_BODY_FIXED, QA_Q3_MOVER_BODY_ATTACHED } qa_q3_shared_mover_kind;
/* TS SharedMoverBody. state.ground may be NULL; it is copied into ground when observed. */
typedef struct qa_q3_shared_mover_body {
  qa_owned_actor actor; qa_q3_shared_mover_kind kind; qa_body_state state; qa_bounds absolute_bounds; double clip_mask;
  bool has_ground; qa_actor_id ground;
} qa_q3_shared_mover_body;
/* TS MoverActorAccess. observe false is TS null; write ground NULL is TS null. */
typedef struct qa_q3_mover_actor_access {
  void *ctx;
  qa_q3_game_entity *(*native)(void *ctx, qa_actor_id actor);
  qa_q3_damage_participant (*participant)(void *ctx, qa_actor_id actor);
  bool (*observe)(void *ctx, qa_actor_id actor, qa_q3_shared_mover_body *out);
  void (*write)(void *ctx, qa_owned_actor actor, qa_vec3 origin, const qa_actor_id *ground);
  void (*link)(void *ctx, qa_owned_actor actor);
  void (*release)(void *ctx, qa_owned_actor actor);
} qa_q3_mover_actor_access;
/* TS MoverHost. combat is a function so time stays live; previous_time is refreshed by the owner.
 * has_missionpack false is TS missionpack null. */
typedef struct qa_q3_mover_host {
  void *ctx; qa_q3_base_product product; double previous_time;
  qa_q3_game_combat_context *(*combat)(void *ctx);
  qa_q3_base_server_world world; const qa_q3_base_actor_spatial_queries *spatial;
  qa_q3_mover_actor_access actors; qa_q3_config_string_registry *config; qa_shared_body_table *bodies;
  void (*use_targets)(void *ctx, qa_q3_game_entity *entity, qa_q3_use_participant activator);
  void (*adjust_area_portal_state)(void *ctx, qa_q3_game_entity *entity, bool open);
  void (*return_dropped_flag)(void *ctx, qa_q3_game_entity *entity);
  bool has_missionpack; void (*explode_missile)(void *ctx, qa_q3_game_entity *entity);
} qa_q3_mover_host;
typedef struct qa_q3_mover_runtime { qa_q3_mover_host host; bool bound; } qa_q3_mover_runtime;
/* Stores the host; performs the TS constructor work when host.combat is set. */
void qa_q3_mover_runtime_init(qa_q3_mover_runtime *runtime, qa_q3_mover_host host);
/* TS constructor: product check and bindSaveCallbacks. Idempotent. */
void qa_q3_mover_runtime_bind(qa_q3_mover_runtime *runtime);
void qa_q3_mover_bind_save_callbacks(qa_q3_mover_runtime *runtime);
/* Returns false for TS null (position clear). */
bool qa_q3_mover_test_entity_position(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity, qa_q3_damage_participant *out);
void qa_q3_mover_run_team(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_mover_run(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_mover_set_state(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity, qa_q3_mover_state state, double time);
void qa_q3_mover_match_team(qa_q3_mover_runtime *runtime, qa_q3_game_entity *leader, qa_q3_mover_state state, double time);
void qa_q3_mover_return_to_pos1(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_mover_reached_binary(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_mover_use_binary(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity, const qa_q3_use_participant *other, const qa_q3_use_participant *activator);
void qa_q3_mover_initialize_binary(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables);
void qa_q3_mover_blocked_door(qa_q3_mover_runtime *runtime, qa_q3_game_entity *entity, qa_q3_damage_participant other);
#endif
