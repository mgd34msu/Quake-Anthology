/* Port of src/app/bootstrap/simulation/bot-rerelease-world.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_rerelease_bot_world qa_app_sim_rerelease_bot_world;
typedef struct qa_app_sim_rerelease_bot_world_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_rerelease_bot_world_options;
qa_app_sim_rerelease_bot_world *qa_app_sim_rerelease_bot_world_create(qa_app_sim_rerelease_bot_world_options options);
void qa_app_sim_rerelease_bot_world_destroy(qa_app_sim_rerelease_bot_world *value);
#endif
