/* Port of src/app/bootstrap/simulation/equipment-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_EQUIPMENT_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_EQUIPMENT_CHECKPOINT_H
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include "qa/persistence/value.h"
qa_app_sim_hand_grenade_runtime_checkpoint qa_app_sim_read_hand_grenade_runtime_checkpoint(const qa_save_reader *reader);
#endif
