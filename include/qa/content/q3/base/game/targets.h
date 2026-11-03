/* Port of src/content/q3/base/game/targets.ts. GPL-2.0-or-later.
 * Item touch is host fn+ctx (ItemLifecycleContext in TS); save
 * capture/restore of locations is reduced to linked+head slot values. */
#ifndef QA_CONTENT_Q3_BASE_GAME_TARGETS_H
#define QA_CONTENT_Q3_BASE_GAME_TARGETS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/use_participant.h"
/* TS TargetLocationState. */
typedef struct qa_q3_target_location_state { bool linked; qa_q3_game_entity *head; } qa_q3_target_location_state;
void qa_q3_target_location_state_init(qa_q3_target_location_state *state);
void qa_q3_target_location_state_reset(qa_q3_target_location_state *state);
typedef struct qa_q3_target_location_save { bool linked; bool has_head; double head_slot; } qa_q3_target_location_save;
qa_q3_target_location_save qa_q3_target_location_capture(const qa_q3_target_location_state *state);
void qa_q3_target_location_restore(qa_q3_target_location_state *state, qa_q3_target_location_save save, qa_q3_entity_pool *pool);
/* TS TargetRuntime (host interface as fn+ctx). */
typedef struct qa_q3_target_runtime {
  void *ctx; qa_q3_entity_pool *entities; qa_q3_target_location_state location;
  qa_q3_base_server_world world; qa_q3_kill_box_host kill_box;
  bool has_participants; qa_q3_use_participant_services participants;
  bool item_lifecycle_matches;
  qa_q3_game_combat_context *(*combat)(void *ctx);
  double (*gravity)(void *ctx); double (*rand_value)(void *ctx); double (*crandom)(void *ctx);
  double (*sound_index)(void *ctx, const char *path);
  void (*add_score)(void *ctx, qa_q3_game_entity *player, qa_vec3 origin, double points);
  void (*return_flag)(void *ctx, double team);
  void (*send_server_command)(void *ctx, double client_num, const char *command);
  void (*remap_shader)(void *ctx, const char *old_name, const char *new_name, double time_seconds);
  void (*set_configstring)(void *ctx, double index, const char *value);
  void (*warn)(void *ctx, const char *message);
  void (*touch_item)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *activator);
} qa_q3_target_runtime;
void qa_q3_target_runtime_init(qa_q3_target_runtime *runtime, void *ctx, qa_q3_entity_pool *entities);
size_t qa_q3_target_spawn_handlers(qa_q3_target_runtime *runtime, qa_q3_spawn_handler_entry *out, size_t out_cap);
void qa_q3_bind_target_save_callbacks(qa_q3_target_runtime *runtime);
#endif
