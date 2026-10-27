/* Port of src/content/q2/missionpacks/monsters/power-armor.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_POWER_ARMOR_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_POWER_ARMOR_H

#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/world/gameplay/authority.h"

/* Mirrored unported surface (TS owners: src/world/gameplay/
 * authority.ts GameplayAuthority.bindPowerArmorCells/
 * setPoweredProtection and src/world/gameplay/inventory.ts
 * SharedInventoryTable.has/create; the qa_q2f combat/inventory
 * mirrors lack these members). The host wires hooks with
 * qa_q2mpm_set_power_hooks; use without them aborts. kind is
 * "screen" | "shield". */
typedef struct qa_q2mpm_power_hooks {
  void *ctx;
  void (*bind_cells)(void *ctx, qa_owned_actor actor, qa_power_armor_cell_binding cells);
  void (*set_powered)(void *ctx, qa_owned_actor actor, const char *kind, double cells);
  bool (*inventory_has)(void *ctx, qa_actor_id actor);
  void (*inventory_create_empty)(void *ctx, qa_owned_actor actor);
} qa_q2mpm_power_hooks;
void qa_q2mpm_set_power_hooks(qa_q2mpm_power_hooks hooks);

void qa_q2mpm_monster_power_armor(qa_q2m_context *context, const char *kind, double cells);
void qa_q2mpm_restore_monster_power_armor(qa_q2m_context *context);
void qa_q2mpm_restore_monster_power_armor_handler(void *ctx, qa_q2m_context *context);

#endif
