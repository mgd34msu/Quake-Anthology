/* Port of src/app/bootstrap/simulation/grapple-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_CHECKPOINT_H
#include "qa/persistence/value.h"
/* Checkpoint payload stays an owned save-value tree until grapple-runtime lands its struct. */
qa_save_value *qa_app_sim_read_grapple_runtime_checkpoint(const qa_save_reader *reader);
#endif
