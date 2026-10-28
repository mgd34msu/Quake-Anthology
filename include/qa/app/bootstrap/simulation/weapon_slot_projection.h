/* Port of src/app/bootstrap/simulation/weapon-slot-projection.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_WEAPON_SLOT_PROJECTION_H
#define QA_APP_BOOTSTRAP_SIMULATION_WEAPON_SLOT_PROJECTION_H
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include "qa/contracts/content.h"
typedef struct qa_app_sim_equipment_weapon_projection {
  qa_provider_reference source; qa_weapon_reference weapon;
  qa_app_sim_player_ui_item item;
  const qa_app_simulation_presentation *model; /* nullable */
} qa_app_sim_equipment_weapon_projection;
typedef struct qa_app_sim_weapon_slot_projection {
  const qa_weapon_reference *active; const qa_weapon_reference *pending; /* nullable */
  qa_app_sim_player_ui ui; /* ui.items is malloc'd by project; caller frees */
  const qa_app_simulation_presentation *model;
} qa_app_sim_weapon_slot_projection;
typedef qa_app_sim_weapon_slot_projection qa_app_sim_primary_weapon_projection;
typedef struct qa_app_sim_source_weapon_projection {
  qa_provider_id provider;
  const qa_weapon_reference *active; const qa_weapon_reference *pending;
  const qa_app_sim_player_ui_item *items; size_t item_count;
  const qa_weapon_hud_status *weapon_status;
  bool has_ammo; qa_item_id ammo_item; double ammo_count;
  const qa_app_simulation_presentation *model;
} qa_app_sim_source_weapon_projection;
/* equipment may be NULL. */
qa_app_sim_weapon_slot_projection qa_app_sim_project_weapon_slot(qa_app_sim_weapon_slot_state state,
    qa_app_sim_primary_weapon_projection primary, const qa_app_sim_equipment_weapon_projection *equipment,
    const qa_app_sim_source_weapon_projection *sources, size_t source_count);
#endif
