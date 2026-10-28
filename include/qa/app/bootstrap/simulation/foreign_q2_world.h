/* Port of src/app/bootstrap/simulation/foreign-q2-world.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_Q2_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_Q2_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_foreign_q2_world_modules qa_app_sim_foreign_q2_world_modules;
typedef struct qa_app_sim_foreign_q2_world_modules_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_foreign_q2_world_modules_options;
qa_app_sim_foreign_q2_world_modules *qa_app_sim_foreign_q2_world_modules_create(qa_app_sim_foreign_q2_world_modules_options options);
void qa_app_sim_foreign_q2_world_modules_destroy(qa_app_sim_foreign_q2_world_modules *value);
#endif
