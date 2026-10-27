/* Port of src/content/q3/base/game/mover.ts. GPL-2.0-or-later.
 * MoverRuntime push/ride logic needs ServerWorld actor access (unresolved). */
#ifndef QA_CONTENT_Q3_BASE_GAME_MOVER_H
#define QA_CONTENT_Q3_BASE_GAME_MOVER_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
typedef struct qa_q3_shared_mover_body { qa_vec3 origin, angles; qa_bounds bounds; } qa_q3_shared_mover_body;
typedef struct qa_q3_mover_actor_access { void *ctx; } qa_q3_mover_actor_access;
typedef struct qa_q3_mover_host { void *ctx; qa_q3_game_product product; } qa_q3_mover_host;
typedef struct qa_q3_mover_runtime { qa_q3_mover_host host; } qa_q3_mover_runtime;
void qa_q3_mover_runtime_init(qa_q3_mover_runtime *runtime, qa_q3_mover_host host);
#endif
