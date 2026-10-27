/* Port of src/content/composition/q2-expansion-arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2_expansion_arsenal.h"

#include <string.h>

const char *qa_comp_selected_q2_program_name(qa_comp_selected_q2_program program) {
  static const char *const NAMES[] = {"baseq2", "xatrix", "rogue", "mg2"};
  return NAMES[program];
}

/* Shared pickup preference, weakest first (composition policy). */
static const qa_item_id PICKUP_ORDER[] = {
    "q2:weapon_blaster", "q2:weapon_chainfist", "q2:weapon_shotgun", "q2:weapon_supershotgun",
    "q2:weapon_machinegun", "q2:weapon_etf_rifle", "q2:weapon_chaingun", "q2:ammo_grenades", "q2:ammo_trap",
    "q2:ammo_tesla", "q2:weapon_grenadelauncher", "q2:weapon_proxlauncher", "q2:weapon_rocketlauncher",
    "q2:weapon_hyperblaster", "q2:weapon_boomer", "q2:weapon_plasmabeam", "q2:weapon_railgun",
    "q2:weapon_phalanx", "q2:weapon_disintegrator", "q2:weapon_bfg"};
static const char *const FALLBACK_ORDER[] = {
    "disintegrator", "railgun", "heatbeam", "ionripper", "hyperblaster", "etf_rifle", "chaingun", "machinegun",
    "supershotgun", "shotgun", "phalanx", "rocketlauncher", "grenadelauncher", "proxlauncher", "chainfist", "blaster"};

bool qa_comp_register_selected_q2_mission_weapons(
    const qa_comp_q2_arsenal_hooks *hooks, void *weapons, qa_comp_selected_q2_program program,
    const void *projectile_hooks, qa_comp_q2_inventory_definition *definitions_out, size_t definitions_capacity,
    size_t *out_definition_count, qa_item_id *pickup_order_out, size_t pickup_capacity, size_t *out_pickup_count) {
  void *ctx = hooks->ctx;
  bool rerelease = strcmp(hooks->edition, "rerelease") == 0;
  const char *packs[2];
  size_t pack_count = 0;
  if (rerelease) { packs[0] = "xatrix"; packs[1] = "rogue"; pack_count = 2; }
  else if (program == QA_COMP_SELECTED_Q2_XATRIX || program == QA_COMP_SELECTED_Q2_ROGUE) {
    packs[0] = qa_comp_selected_q2_program_name(program);
    pack_count = 1;
  }
  void *projectiles = hooks->create_projectiles(ctx, projectile_hooks, weapons);
  void *extensions = hooks->create_extensions(ctx, projectiles);
  for (size_t i = 0; i < pack_count; i++) hooks->register_pack(ctx, extensions, weapons, packs[i], hooks->edition);
  hooks->register_callbacks(ctx, hooks->projectile_callbacks(ctx, projectiles));
  if (rerelease) hooks->set_fallback_order(ctx, weapons, FALLBACK_ORDER, sizeof FALLBACK_ORDER / sizeof *FALLBACK_ORDER);
  size_t definition_count = 0;
  const qa_item_id *definition_items = hooks->registered_definition_items(ctx, weapons, &definition_count);
  size_t base_count = 0, total = 0;
  const qa_comp_q2_inventory_definition *base = hooks->base_weapon_inventory(ctx, &base_count);
  total += base_count;
  const qa_comp_q2_inventory_definition *mission[2];
  size_t mission_count[2] = {0, 0};
  for (size_t i = 0; i < pack_count; i++) {
    mission[i] = hooks->mission_weapon_inventory(ctx, packs[i], &mission_count[i]);
    total += mission_count[i];
  }
  if (total > definitions_capacity) return false;
  size_t pickup_total = 0;
  for (size_t i = 0; i < sizeof PICKUP_ORDER / sizeof *PICKUP_ORDER; i++)
    for (size_t d = 0; d < definition_count; d++)
      if (strcmp(definition_items[d], PICKUP_ORDER[i]) == 0) { pickup_total++; break; }
  if (pickup_total > pickup_capacity) return false;
  size_t have = 0;
  for (size_t i = 0; i < base_count; i++) definitions_out[have++] = base[i];
  for (size_t p = 0; p < pack_count; p++)
    for (size_t i = 0; i < mission_count[p]; i++) definitions_out[have++] = mission[p][i];
  *out_definition_count = have;
  have = 0;
  for (size_t i = 0; i < sizeof PICKUP_ORDER / sizeof *PICKUP_ORDER; i++)
    for (size_t d = 0; d < definition_count; d++)
      if (strcmp(definition_items[d], PICKUP_ORDER[i]) == 0) { pickup_order_out[have++] = PICKUP_ORDER[i]; break; }
  *out_pickup_count = have;
  return true;
}
