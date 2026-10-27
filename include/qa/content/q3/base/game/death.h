/* Port of src/content/q3/base/game/death.ts. GPL-2.0-or-later.
 * Persistent-powerup helpers ported; DeathRuntime body needs DeathHost world/teams (unresolved). */
#ifndef QA_CONTENT_Q3_BASE_GAME_DEATH_H
#define QA_CONTENT_Q3_BASE_GAME_DEATH_H
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/state.h"
void qa_q3_return_persistent_powerup(qa_q3_game_entity *powerup, qa_q3_server_world_mirror world);
typedef void (*qa_q3_return_item_fn)(void *ctx, qa_q3_game_entity *powerup);
void qa_q3_toss_client_persistent_powerup(qa_q3_game_entity *entity, qa_q3_return_item_fn return_item, void *return_ctx);
typedef struct qa_q3_death_frame { double time, frame_time; } qa_q3_death_frame;
typedef struct qa_q3_death_host { void *ctx; qa_q3_game_product product; } qa_q3_death_host;
typedef struct qa_q3_death_runtime { qa_q3_death_host host; } qa_q3_death_runtime;
void qa_q3_death_runtime_init(qa_q3_death_runtime *runtime, qa_q3_death_host host);
#endif
