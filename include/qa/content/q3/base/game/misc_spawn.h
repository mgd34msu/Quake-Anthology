/* Port of src/content/q3/base/game/misc-spawn.ts. GPL-2.0-or-later. Handler bodies unresolved. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MISC_SPAWN_H
#define QA_CONTENT_Q3_BASE_GAME_MISC_SPAWN_H
#include <stddef.h>
#include "qa/content/q3/base/game/spawn.h"
typedef struct qa_q3_misc_spawn_host { void *ctx; } qa_q3_misc_spawn_host;
size_t qa_q3_misc_spawn_handlers(qa_q3_misc_spawn_host host, qa_q3_spawn_handler_entry *out, size_t out_cap);
#endif
