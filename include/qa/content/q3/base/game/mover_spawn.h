/* Port of src/content/q3/base/game/mover-spawn.ts. GPL-2.0-or-later.
 * MoverRuntime core calls (initializeBinary/useBinary/blockedDoor/
 * matchTeam/setState) and SV_SetBrushModel/config are host fn+ctx:
 * landed mover.h is an opaque mirror (TS owner ./mover.ts). */
#ifndef QA_CONTENT_Q3_BASE_GAME_MOVER_SPAWN_H
#define QA_CONTENT_Q3_BASE_GAME_MOVER_SPAWN_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/mover.h"
#include "qa/content/q3/base/game/spawn.h"
typedef struct qa_q3_mover_spawn_host {
  void *ctx; qa_q3_mover_runtime *movers; qa_q3_entity_pool *pool;
  qa_q3_base_server_world world; qa_q3_teleport_context teleport;
  double (*time)(void *ctx); double (*gravity)(void *ctx);
  void (*set_brush_model)(void *ctx, qa_q3_game_entity *entity, const char *name);
  void (*remap_shader)(void *ctx, const char *old_name, const char *new_name, double time_seconds);
  void (*warn)(void *ctx, const char *message);
  double (*sound_index)(void *ctx, const char *path);
  void (*initialize_binary)(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables);
  void (*use_binary)(void *ctx, qa_q3_game_entity *parent, qa_q3_game_entity *entity, qa_q3_damage_participant other);
  void (*blocked_door)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other);
  void (*match_team)(void *ctx, qa_q3_game_entity *entity, double state, double time);
  void (*set_state)(void *ctx, qa_q3_game_entity *entity, double state, double time);
} qa_q3_mover_spawn_host;
typedef struct qa_q3_mover_spawn_runtime { qa_q3_mover_spawn_host host; } qa_q3_mover_spawn_runtime;
void qa_q3_mover_spawn_runtime_init(qa_q3_mover_spawn_runtime *runtime, qa_q3_mover_spawn_host host);
void qa_q3_mover_spawn_bind_save_callbacks(qa_q3_mover_spawn_runtime *runtime);
bool qa_q3_mover_spawn_is_door_trigger(qa_q3_mover_spawn_runtime *runtime, qa_q3_game_entity *entity);
size_t qa_q3_mover_spawn_runtime_handlers(qa_q3_mover_spawn_runtime *runtime, qa_q3_spawn_handler_entry *out, size_t out_cap);
#endif
