/* Port of src/content/q3/base/game/targets.ts. GPL-2.0-or-later.
 * TargetLocationState ported; handler bodies need full TargetRuntime host (unresolved). */
#ifndef QA_CONTENT_Q3_BASE_GAME_TARGETS_H
#define QA_CONTENT_Q3_BASE_GAME_TARGETS_H
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/spawn.h"
typedef struct qa_q3_target_location_state { qa_q3_game_entity *location; double last_update; } qa_q3_target_location_state;
void qa_q3_target_location_state_init(qa_q3_target_location_state *state);
typedef struct qa_q3_target_runtime { void *ctx; qa_q3_entity_pool *entities; qa_q3_target_location_state location; } qa_q3_target_runtime;
void qa_q3_target_runtime_init(qa_q3_target_runtime *runtime, void *ctx, qa_q3_entity_pool *entities);
size_t qa_q3_target_spawn_handlers(qa_q3_target_runtime *runtime, qa_q3_spawn_handler_entry *out, size_t out_cap);
void qa_q3_bind_target_save_callbacks(qa_q3_target_runtime *runtime);
#endif
