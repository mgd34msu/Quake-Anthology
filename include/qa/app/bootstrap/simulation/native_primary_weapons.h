/* Port of src/app/bootstrap/simulation/native-primary-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_PRIMARY_WEAPONS_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_PRIMARY_WEAPONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_native_primary_weapons qa_app_sim_native_primary_weapons;
typedef struct qa_app_sim_native_primary_weapons_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_native_primary_weapons_options;
qa_app_sim_native_primary_weapons *qa_app_sim_native_primary_weapons_create(qa_app_sim_native_primary_weapons_options options);
void qa_app_sim_native_primary_weapons_destroy(qa_app_sim_native_primary_weapons *value);
#endif
