/* Port of src/content/q3/base/game/triggers.ts. GPL-2.0-or-later.
 * touchJumpPad host fn: landed shared/jump_pad.h needs the real player/
 * entity records, incompatible with the game mirror (TS owner
 * ../shared/jump-pad.ts). Combat spatial identity check in TS cannot be
 * expressed against the world mirror and is reduced to pool+time. */
#ifndef QA_CONTENT_Q3_BASE_GAME_TRIGGERS_H
#define QA_CONTENT_Q3_BASE_GAME_TRIGGERS_H
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/use_participant.h"
#include "qa/content/q3/base/game/utilities.h"
typedef struct qa_q3_trigger_host {
  void *ctx; qa_q3_entity_pool *entities;
  qa_q3_server_world_mirror world; qa_q3_kill_box_host kill_box;
  bool has_participants; qa_q3_use_participant_services participants;
  qa_q3_game_combat_context *(*combat)(void *ctx);
  double (*gravity)(void *ctx); double (*rand_value)(void *ctx); double (*crandom)(void *ctx);
  void (*set_brush_model)(void *ctx, qa_q3_game_entity *entity, const char *name);
  double (*sound_index)(void *ctx, const char *path);
  void (*remap_shader)(void *ctx, const char *old_name, const char *new_name, double time_seconds);
  void (*warn)(void *ctx, const char *message);
  void (*touch_jump_pad)(void *ctx, qa_q3_game_client *client, qa_q3_game_entity *trigger);
  bool (*can_take_damage)(void *ctx, qa_actor_id actor);
} qa_q3_trigger_host;
typedef struct qa_q3_trigger_runtime { qa_q3_trigger_host host; } qa_q3_trigger_runtime;
void qa_q3_trigger_runtime_init(qa_q3_trigger_runtime *runtime, qa_q3_trigger_host host);
typedef struct qa_q3_aim_at_target_context {
  qa_q3_target_selection_context selection; void *gravity_ctx; double (*gravity)(void *ctx);
} qa_q3_aim_at_target_context;
void qa_q3_aim_at_target(qa_q3_aim_at_target_context context, qa_q3_game_entity *entity, qa_vec3 origin);
size_t qa_q3_trigger_spawn_handlers(qa_q3_trigger_host host, qa_q3_spawn_handler_entry *out, size_t out_cap);
void qa_q3_bind_trigger_save_callbacks(qa_q3_trigger_host host);
void qa_q3_trigger_runtime_bind_save_callbacks(qa_q3_trigger_runtime *runtime);
#endif
