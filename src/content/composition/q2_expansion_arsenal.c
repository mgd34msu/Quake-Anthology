/* Port of src/content/composition/q2-expansion-arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2_expansion_arsenal.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/missionpacks/items.h"

const char *qa_comp_selected_q2_program_name(qa_comp_selected_q2_program program) {
  static const char *const NAMES[] = {"baseq2", "xatrix", "rogue", "mg2"};
  return NAMES[program];
}

/* Shared pickup preference, weakest first. This is a composition policy, not native Q2 autoswitch. */
static const qa_item_id PICKUP_ORDER[] = {
    "q2:weapon_blaster", "q2:weapon_chainfist", "q2:weapon_shotgun", "q2:weapon_supershotgun",
    "q2:weapon_machinegun", "q2:weapon_etf_rifle", "q2:weapon_chaingun", "q2:ammo_grenades", "q2:ammo_trap",
    "q2:ammo_tesla", "q2:weapon_grenadelauncher", "q2:weapon_proxlauncher", "q2:weapon_rocketlauncher",
    "q2:weapon_hyperblaster", "q2:weapon_boomer", "q2:weapon_plasmabeam", "q2:weapon_railgun",
    "q2:weapon_phalanx", "q2:weapon_disintegrator", "q2:weapon_bfg"};
static const qa_q2w_name FALLBACK_ORDER[] = {
    "disintegrator", "railgun", "heatbeam", "ionripper", "hyperblaster", "etf_rifle", "chaingun", "machinegun",
    "supershotgun", "shotgun", "phalanx", "rocketlauncher", "grenadelauncher", "proxlauncher", "chainfist", "blaster"};

bool qa_comp_register_selected_q2_mission_weapons(qa_comp_selected_q2_mission_weapons *out, qa_q2_game_services *game,
    qa_q2w_weapons *weapons, qa_comp_selected_q2_program program, qa_q2mp_projectile_hooks hooks) {
  memset(out, 0, sizeof *out);
  qa_q2_edition edition = game->options.edition;
  qa_q2mp_pack packs[2];
  size_t pack_count = 0;
  if (edition == QA_Q2_EDITION_RERELEASE) { packs[0] = QA_Q2MP_PACK_XATRIX; packs[1] = QA_Q2MP_PACK_ROGUE; pack_count = 2; }
  else if (program == QA_COMP_SELECTED_Q2_XATRIX) { packs[0] = QA_Q2MP_PACK_XATRIX; pack_count = 1; }
  else if (program == QA_COMP_SELECTED_Q2_ROGUE) { packs[0] = QA_Q2MP_PACK_ROGUE; pack_count = 1; }
  /* new Q2MissionPackProjectiles({ ...hooks, base: weapons }) */
  hooks.base = &weapons->base;
  qa_q2mp_projectiles_init(&out->projectiles, hooks);
  qa_q2mp_weapons_init(&out->extensions, &out->projectiles);
  for (size_t i = 0; i < pack_count; i++) qa_q2mp_weapons_register(&out->extensions, weapons, packs[i], edition);
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_projectiles_callbacks(&out->projectiles));
  if (edition == QA_Q2_EDITION_RERELEASE) qa_q2w_weapons_set_fallback_order(weapons, FALLBACK_ORDER, sizeof FALLBACK_ORDER / sizeof *FALLBACK_ORDER);
  size_t definition_count = 0;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(weapons, &definition_count);
  /* [...q2BaseWeaponInventory(), ...packs.flatMap(pack => q2MissionWeaponInventory(pack, game.options.edition))] */
  size_t base_count = 0;
  const qa_q2_weapon_inventory_entry *base = qa_q2_base_weapon_inventory(&base_count);
  size_t total = base_count;
  for (size_t p = 0; p < pack_count; p++) total += qa_q2mp_mission_weapon_inventory_edition(packs[p], edition, NULL, 0);
  out->inventory_definitions = malloc((total ? total : 1) * sizeof *out->inventory_definitions);
  out->pickup_order = malloc(sizeof PICKUP_ORDER);
  if (out->inventory_definitions == NULL || out->pickup_order == NULL) { qa_comp_selected_q2_mission_weapons_destroy(out); return false; }
  for (size_t i = 0; i < base_count; i++) out->inventory_definitions[out->inventory_definition_count++] = (qa_comp_q2_inventory_definition){base[i].item, base[i].capacity};
  for (size_t p = 0; p < pack_count; p++) {
    qa_q2mp_weapon_inventory_entry entries[32];
    size_t count = qa_q2mp_mission_weapon_inventory_edition(packs[p], edition, entries, 32);
    for (size_t i = 0; i < count && i < 32; i++)
      out->inventory_definitions[out->inventory_definition_count++] = (qa_comp_q2_inventory_definition){entries[i].item, entries[i].capacity};
  }
  /* pickupOrder.filter(item => definitions.some(definition => definition.item === item)) */
  for (size_t i = 0; i < sizeof PICKUP_ORDER / sizeof *PICKUP_ORDER; i++)
    for (size_t d = 0; d < definition_count; d++)
      if (definitions[d]->item != NULL && strcmp(definitions[d]->item, PICKUP_ORDER[i]) == 0) { out->pickup_order[out->pickup_order_count++] = PICKUP_ORDER[i]; break; }
  return true;
}

void qa_comp_selected_q2_mission_weapons_destroy(qa_comp_selected_q2_mission_weapons *mission) {
  free(mission->inventory_definitions); mission->inventory_definitions = NULL; mission->inventory_definition_count = 0;
  free(mission->pickup_order); mission->pickup_order = NULL; mission->pickup_order_count = 0;
}
