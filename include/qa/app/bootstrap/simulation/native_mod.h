/* Port of src/app/bootstrap/simulation/native-mod.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_native_mod_preparation qa_app_sim_native_mod_preparation;
typedef struct qa_app_sim_native_mod_preparation_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_native_mod_preparation_options;
qa_app_sim_native_mod_preparation *qa_app_sim_native_mod_preparation_create(qa_app_sim_native_mod_preparation_options options);
void qa_app_sim_native_mod_preparation_destroy(qa_app_sim_native_mod_preparation *value);
#endif
