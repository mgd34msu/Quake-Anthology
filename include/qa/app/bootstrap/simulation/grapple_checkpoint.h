/* Port of src/app/bootstrap/simulation/grapple-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_CHECKPOINT_H
#include "qa/app/bootstrap/simulation/grapple_runtime.h"
#include "qa/persistence/value.h"
/* Strings and bytes borrow from the reader's tree; release with
 * qa_app_sim_grapple_runtime_checkpoint_destroy. */
qa_app_sim_grapple_runtime_checkpoint qa_app_sim_read_grapple_runtime_checkpoint(const qa_save_reader *reader);
#endif
