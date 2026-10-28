/* Port of src/app/bootstrap/simulation/classic-guest-source.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_classic_guest_source qa_app_sim_classic_guest_source;
typedef struct qa_app_sim_classic_guest_source_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_classic_guest_source_options;
qa_app_sim_classic_guest_source *qa_app_sim_classic_guest_source_create(qa_app_sim_classic_guest_source_options options);
void qa_app_sim_classic_guest_source_destroy(qa_app_sim_classic_guest_source *value);
#endif
