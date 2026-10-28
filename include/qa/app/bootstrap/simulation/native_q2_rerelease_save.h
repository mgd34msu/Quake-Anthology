/* Port of src/app/bootstrap/simulation/native-q2-rerelease-save.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_RERELEASE_SAVE_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_RERELEASE_SAVE_H
#include <stddef.h>
#include "qa/persistence/value.h"
typedef struct qa_app_sim_q2_rerelease_native_save { qa_save_value *value; /* owned tree */ } qa_app_sim_q2_rerelease_native_save;
void qa_app_sim_q2_rerelease_native_save_destroy(qa_app_sim_q2_rerelease_native_save *save);
#endif
