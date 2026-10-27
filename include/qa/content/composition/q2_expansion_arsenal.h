/*
 * Port of src/content/composition/q2-expansion-arsenal.ts: shared pickup
 * preference (weakest first) and selected-program mission weapons.
 * Mirrored surfaces from unported modules (qa_comp_* hooks; TS owners:
 * src/content/q2/foundation/host.ts, items.ts, weapons/player.ts;
 * src/content/q2/missionpacks/projectiles + weapons/player + items).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_EXPANSION_ARSENAL_H
#define QA_CONTENT_COMPOSITION_Q2_EXPANSION_ARSENAL_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"

/* TS SelectedQ2Program, in declaration order. */
typedef enum qa_comp_selected_q2_program {
  QA_COMP_SELECTED_Q2_BASEQ2, QA_COMP_SELECTED_Q2_XATRIX, QA_COMP_SELECTED_Q2_ROGUE, QA_COMP_SELECTED_Q2_MG2
} qa_comp_selected_q2_program;
const char *qa_comp_selected_q2_program_name(qa_comp_selected_q2_program program);

typedef struct qa_comp_q2_inventory_definition {
  qa_item_id item;
  double capacity;
} qa_comp_q2_inventory_definition;

typedef struct qa_comp_q2_arsenal_hooks {
  void *ctx;
  const char *edition; /* game.options.edition: "classic" | "rerelease" */
  /* new Q2MissionPackProjectiles({ ...hooks, base: weapons }) */
  void *(*create_projectiles)(void *ctx, const void *projectile_hooks, void *weapons);
  /* new Q2MissionPackWeapons(projectiles); extensions.register(...) */
  void *(*create_extensions)(void *ctx, void *projectiles);
  void (*register_pack)(void *ctx, void *extensions, void *weapons, const char *pack, const char *edition);
  const void *(*projectile_callbacks)(void *ctx, void *projectiles);
  /* game.sourceCallbacks.register(callbacks) */
  void (*register_callbacks)(void *ctx, const void *callbacks);
  void (*set_fallback_order)(void *ctx, void *weapons, const char *const *order, size_t count);
  /* weapons.registeredDefinitions(): item strings, for pickup filtering */
  const qa_item_id *(*registered_definition_items)(void *ctx, void *weapons, size_t *out_count);
  /* q2BaseWeaponInventory() / q2MissionWeaponInventory(pack) */
  const qa_comp_q2_inventory_definition *(*base_weapon_inventory)(void *ctx, size_t *out_count);
  const qa_comp_q2_inventory_definition *(*mission_weapon_inventory)(void *ctx, const char *pack,
                                                                      size_t *out_count);
} qa_comp_q2_arsenal_hooks;

/**
 * TS registerSelectedQ2MissionWeapons(). projectile_hooks is the opaque
 * Omit<Q2MissionPackProjectileHooks, "base"> forwarded to the projectile
 * constructor. The result rows are written into caller storage; false
 * when either array cannot hold the full result.
 */
bool qa_comp_register_selected_q2_mission_weapons(
    const qa_comp_q2_arsenal_hooks *hooks, void *weapons, qa_comp_selected_q2_program program,
    const void *projectile_hooks, qa_comp_q2_inventory_definition *definitions_out, size_t definitions_capacity,
    size_t *out_definition_count, qa_item_id *pickup_order_out, size_t pickup_capacity,
    size_t *out_pickup_count);

#endif
