/* Port of src/app/bootstrap/simulation/native-mod-host.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_HOST_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_native_mod_host qa_app_sim_native_mod_host;
typedef struct qa_app_sim_native_mod_host_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_native_mod_host_options;
qa_app_sim_native_mod_host *qa_app_sim_native_mod_host_create(qa_app_sim_native_mod_host_options options);
void qa_app_sim_native_mod_host_destroy(qa_app_sim_native_mod_host *value);
#endif
