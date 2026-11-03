/*
 * Port of src/content/composition/q2-expansion-arsenal.ts: shared pickup
 * preference (weakest first) and selected-program mission weapons, over the
 * real Q2GameServices, Q2Weapons, Q2MissionPackProjectiles and
 * Q2MissionPackWeapons. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_EXPANSION_ARSENAL_H
#define QA_CONTENT_COMPOSITION_Q2_EXPANSION_ARSENAL_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/projectiles.h"
#include "qa/content/q2/missionpacks/weapons/player.h"

/* TS SelectedQ2Program, in declaration order. */
typedef enum qa_comp_selected_q2_program {
  QA_COMP_SELECTED_Q2_BASEQ2, QA_COMP_SELECTED_Q2_XATRIX, QA_COMP_SELECTED_Q2_ROGUE, QA_COMP_SELECTED_Q2_MG2
} qa_comp_selected_q2_program;
const char *qa_comp_selected_q2_program_name(qa_comp_selected_q2_program program);

typedef struct qa_comp_q2_inventory_definition {
  qa_item_id item;
  double capacity;
} qa_comp_q2_inventory_definition;

/* The registration's owners and its TS result. The projectile and extension
 * owners are referenced by the callbacks the game and weapons now hold, so
 * the record must outlive them (destroy only frees the result arrays). */
typedef struct qa_comp_selected_q2_mission_weapons {
  qa_q2mp_projectiles projectiles;
  qa_q2mp_weapons extensions;
  qa_comp_q2_inventory_definition *inventory_definitions; size_t inventory_definition_count; /* TS inventoryDefinitions */
  qa_item_id *pickup_order; size_t pickup_order_count;                                       /* TS pickupOrder */
} qa_comp_selected_q2_mission_weapons;

/**
 * TS registerSelectedQ2MissionWeapons(game, weapons, program, hooks): hooks
 * is Omit<Q2MissionPackProjectileHooks, "base"> (its base member is replaced
 * by weapons' ballistics). Fills *out in place; false only on OOM.
 */
bool qa_comp_register_selected_q2_mission_weapons(qa_comp_selected_q2_mission_weapons *out, qa_q2_game_services *game,
    qa_q2w_weapons *weapons, qa_comp_selected_q2_program program, qa_q2mp_projectile_hooks hooks);
void qa_comp_selected_q2_mission_weapons_destroy(qa_comp_selected_q2_mission_weapons *mission);

#endif
