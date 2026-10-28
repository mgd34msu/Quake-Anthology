/* Port of src/app/bootstrap/simulation/mod-client-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MOD_CLIENT_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_MOD_CLIENT_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_mod_client_checkpoint qa_app_sim_mod_client_checkpoint;
typedef struct qa_app_sim_mod_client_checkpoint_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_mod_client_checkpoint_options;
qa_app_sim_mod_client_checkpoint *qa_app_sim_mod_client_checkpoint_create(qa_app_sim_mod_client_checkpoint_options options);
void qa_app_sim_mod_client_checkpoint_destroy(qa_app_sim_mod_client_checkpoint *value);
#endif
