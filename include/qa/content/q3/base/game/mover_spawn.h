/* Port of src/content/q3/base/game/mover-spawn.ts. GPL-2.0-or-later. Handler bodies unresolved. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MOVER_SPAWN_H
#define QA_CONTENT_Q3_BASE_GAME_MOVER_SPAWN_H
#include <stddef.h>
#include "qa/content/q3/base/game/mover.h"
#include "qa/content/q3/base/game/spawn.h"
typedef struct qa_q3_mover_spawn_host { void *ctx; qa_q3_mover_runtime *movers; } qa_q3_mover_spawn_host;
typedef struct qa_q3_mover_spawn_runtime { qa_q3_mover_spawn_host host; } qa_q3_mover_spawn_runtime;
void qa_q3_mover_spawn_runtime_init(qa_q3_mover_spawn_runtime *runtime, qa_q3_mover_spawn_host host);
size_t qa_q3_create_mover_spawn_handlers(qa_q3_mover_spawn_host host, qa_q3_spawn_handler_entry *out, size_t out_cap);
#endif
