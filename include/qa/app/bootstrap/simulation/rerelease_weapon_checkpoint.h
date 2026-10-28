/* Port of src/app/bootstrap/simulation/rerelease-weapon-checkpoint.ts.
 * The checkpoint struct is declared with its source in
 * rerelease_weapon_behavior.h. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_WEAPON_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_WEAPON_CHECKPOINT_H
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/persistence/native_weapon.h"
#include "qa/persistence/value.h"
qa_app_sim_rerelease_weapon_behavior_checkpoint qa_app_sim_read_rerelease_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition definition,
    qa_native_weapon_behavior_declaration expected_declaration,
    const qa_native_weapon_profiles *profiles);
#endif
