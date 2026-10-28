/* Port of src/app/bootstrap/simulation/weapon-slot-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_WEAPON_SLOT_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_WEAPON_SLOT_CHECKPOINT_H
#include <stddef.h>
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include "qa/content/q2/equipment/grapple_weapon.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
qa_app_sim_weapon_slot_restore_state qa_app_sim_read_weapon_slot_state(const qa_save_reader *reader);
typedef struct qa_app_sim_saved_weapon_slot { qa_saved_actor_id actor; qa_app_sim_weapon_slot_restore_state state; } qa_app_sim_saved_weapon_slot;
/* Malloc'd array; caller frees. */
qa_app_sim_saved_weapon_slot *qa_app_sim_read_weapon_slots(const qa_save_reader *reader, size_t *out_count);
qa_grapple_weapon_state qa_app_sim_read_grapple_weapon_state(const qa_save_reader *reader);
#endif
