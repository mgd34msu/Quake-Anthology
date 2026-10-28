/* Port of src/app/bootstrap/simulation/bot-rerelease.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_rerelease_bots qa_app_sim_rerelease_bots;
typedef struct qa_app_sim_rerelease_bots_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_rerelease_bots_options;
qa_app_sim_rerelease_bots *qa_app_sim_rerelease_bots_create(qa_app_sim_rerelease_bots_options options);
void qa_app_sim_rerelease_bots_destroy(qa_app_sim_rerelease_bots *value);
#endif
