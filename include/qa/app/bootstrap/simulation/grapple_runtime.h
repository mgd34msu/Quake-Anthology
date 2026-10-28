/* Port of src/app/bootstrap/simulation/grapple-runtime.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_grapple_runtime qa_app_sim_grapple_runtime;
typedef struct qa_app_sim_grapple_runtime_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_grapple_runtime_options;
qa_app_sim_grapple_runtime *qa_app_sim_grapple_runtime_create(qa_app_sim_grapple_runtime_options options);
void qa_app_sim_grapple_runtime_destroy(qa_app_sim_grapple_runtime *value);
#endif
