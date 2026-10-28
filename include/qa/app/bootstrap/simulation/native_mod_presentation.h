/* Port of src/app/bootstrap/simulation/native-mod-presentation.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_native_mod_presentation qa_app_sim_native_mod_presentation;
typedef struct qa_app_sim_native_mod_presentation_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_native_mod_presentation_options;
qa_app_sim_native_mod_presentation *qa_app_sim_native_mod_presentation_create(qa_app_sim_native_mod_presentation_options options);
void qa_app_sim_native_mod_presentation_destroy(qa_app_sim_native_mod_presentation *value);
#endif
