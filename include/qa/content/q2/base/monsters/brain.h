/* Port of src/content/q2/base/monsters/brain.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_BRAIN_H
#define QA_CONTENT_Q2_BASE_MONSTERS_BRAIN_H
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/world/gameplay/authority.h"

/* Mirrored unported surface (TS owners: src/world/gameplay/authority.ts
 * GameplayAuthority.bindPowerArmorCells/setPoweredProtection and
 * src/world/gameplay/inventory.ts SharedInventoryTable.has/create; the
 * qa_q2f combat/inventory mirrors lack these members). The host wires
 * hooks with qa_q2bm_brain_set_power_hooks; use without them aborts.
 * set_powered kind is "screen" (cells meaningful) or "none". */
typedef struct qa_q2bm_brain_power_hooks {
  void *ctx;
  void (*bind_cells)(void *ctx, qa_owned_actor actor, qa_power_armor_cell_binding cells);
  void (*set_powered)(void *ctx, qa_owned_actor actor, const char *kind, double cells);
  bool (*inventory_has)(void *ctx, qa_actor_id actor);
  void (*inventory_create_empty)(void *ctx, qa_owned_actor actor);
} qa_q2bm_brain_power_hooks;
void qa_q2bm_brain_set_power_hooks(qa_q2bm_brain_power_hooks hooks);

extern const qa_q2m_definition qa_q2bm_brain_definition;
#endif
