/* Port of src/app/bootstrap/simulation/bot-world.ts: a sensory projection
 * and ID lookup over existing shared actors, with no entity or physics
 * ownership. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/bots.h"
/* TS createSharedBotWorld options; callbacks receive ctx. actor false is TS null. */
typedef struct qa_app_sim_shared_bot_world_options {
  bool restoring;
  const qa_app_sim_bot_simulation *simulation;
  qa_cvar_registry *cvars;
  void *ctx;
  bool (*actor)(void *ctx, int64_t client, qa_actor_id *out);
  bool (*connect)(void *ctx, int64_t client, bool restart);
  void (*drop)(void *ctx, int64_t client);
  void (*begin)(void *ctx, int64_t client);
  void (*print)(void *ctx, const char *text);
  void (*console)(void *ctx, const char *text);
  void (*message)(void *ctx, int64_t client, const char *text);
} qa_app_sim_shared_bot_world_options;
typedef struct qa_app_sim_shared_bot_world qa_app_sim_shared_bot_world;
/* TS createSharedBotWorld(options); TS throws raise. NULL only on OOM. */
qa_app_sim_shared_bot_world *qa_app_sim_shared_bot_world_create(const qa_app_sim_shared_bot_world_options *options);
void qa_app_sim_shared_bot_world_destroy(qa_app_sim_shared_bot_world *world);
/* The returned members: game (SourceBotGame), knowledge (the arsenal binding). */
qa_source_bot_game_bots_a *qa_app_sim_shared_bot_world_game(qa_app_sim_shared_bot_world *world);
qa_app_sim_bot_arsenal *qa_app_sim_shared_bot_world_knowledge(qa_app_sim_shared_bot_world *world);
int64_t qa_app_sim_shared_bot_world_entity_id(qa_app_sim_shared_bot_world *world, qa_actor_id actor);
bool qa_app_sim_shared_bot_world_actor_for_id(qa_app_sim_shared_bot_world *world, int64_t number, qa_actor_id *out);
void qa_app_sim_shared_bot_world_refresh(qa_app_sim_shared_bot_world *world);
qa_save_value *qa_app_sim_shared_bot_world_checkpoint(qa_app_sim_shared_bot_world *world);
void qa_app_sim_shared_bot_world_restore_checkpoint(qa_app_sim_shared_bot_world *world, const qa_save_value *value,
    qa_app_sim_saved_actor_ref_fn actor, void *actor_ctx);
#endif
