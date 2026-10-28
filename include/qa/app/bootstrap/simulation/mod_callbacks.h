/* Port of src/app/bootstrap/simulation/mod-callbacks.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MOD_CALLBACKS_H
#define QA_APP_BOOTSTRAP_SIMULATION_MOD_CALLBACKS_H
#include "qa/contracts/time.h"
double qa_app_sim_mod_callback_seconds(qa_source_time time);
typedef struct qa_app_sim_mod_callback_registrar { void *ctx; const void *registrations; } qa_app_sim_mod_callback_registrar;
#endif
