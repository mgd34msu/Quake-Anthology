/* Port of src/content/q3/equipment/cgame-weapon-hud.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_CGAME_WEAPON_HUD_H
#define QA_CONTENT_Q3_EQUIPMENT_CGAME_WEAPON_HUD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
typedef struct qa_q3_equipment_hud_ammo { double entry, join; } qa_q3_equipment_hud_ammo;
typedef struct qa_q3_equipment_hud_region {
  double entry, decision; bool taken;
  const qa_q3_equipment_hud_ammo *ammo; size_t ammo_count;
} qa_q3_equipment_hud_region;
typedef struct qa_q3_equipment_qvm_presentation_profile {
  double hud;
  double warning_entry, warning_state, warning_none, warning_low, warning_empty;
  bool status_is_regions; /* false is TS "functions" */
  const qa_q3_equipment_hud_region *status_regions; size_t status_region_count;
  const double *status_functions; size_t status_function_count;
  double held_entry, held_gun, held_parent_argument, held_state_argument, held_entity_argument, held_entity_number_offset;
  double view_entry, view_decision; bool view_taken;
} qa_q3_equipment_qvm_presentation_profile;
bool qa_q3_equipment_presentation_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_presentation_profile *out);
#endif
