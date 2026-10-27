/* Port of src/content/q3/base/game/triggers.ts. GPL-2.0-or-later.
 * aimAtTarget fully ported; spawn-handler bodies need ServerWorld/combat hosts (unresolved). */
#ifndef QA_CONTENT_Q3_BASE_GAME_TRIGGERS_H
#define QA_CONTENT_Q3_BASE_GAME_TRIGGERS_H
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/utilities.h"
typedef struct qa_q3_trigger_host { void *ctx; qa_q3_entity_pool *entities; } qa_q3_trigger_host;
typedef struct qa_q3_aim_at_target_context {
  qa_q3_target_selection_context selection; void *gravity_ctx; double (*gravity)(void *ctx);
} qa_q3_aim_at_target_context;
void qa_q3_aim_at_target(qa_q3_aim_at_target_context context, qa_q3_game_entity *entity, qa_vec3 origin);
/* Handler table: entries filled, count returned. Handlers abort until host wiring lands. */
size_t qa_q3_trigger_spawn_handlers(qa_q3_trigger_host host, qa_q3_spawn_handler_entry *out, size_t out_cap);
void qa_q3_bind_trigger_save_callbacks(qa_q3_trigger_host host);
#endif
