/* Port of src/movement/q3/weapon.ts (PM_Weapon). GPL-2.0-or-later.
 * ammo get/set and externalSlot become fn+ctx / pointer phase. */
#ifndef QA_MOVEMENT_Q3_WEAPON_H
#define QA_MOVEMENT_Q3_WEAPON_H
#include <stdbool.h>
#include "qa/movement/q3/types.h"

double qa_q3_weapon_delay(double milliseconds, double persistent, bool haste);

/* TS Q3ExternalWeaponSlot exact strings in comments. */
typedef enum qa_q3_external_weapon_slot { QA_Q3_WEAPON_SLOT_ACTIVE, QA_Q3_WEAPON_SLOT_HOLSTER_REQUESTED,
  QA_Q3_WEAPON_SLOT_DROPPING, QA_Q3_WEAPON_SLOT_HOLSTERED, QA_Q3_WEAPON_SLOT_RESUME_REQUESTED } qa_q3_external_weapon_slot;

typedef struct qa_q3_source_weapon_state {
  void *ammo_ctx;
  const char *product; /* "baseq3" | "missionpack" */
  double pm_flags, weapon, weapon_state, weapon_time;
  double owned_weapons, health, max_health;
  bool spectator, haste; double persistent_powerup_tag;
  double holdable_item, holdable_tag;
  double (*ammo_get)(void *ctx, double weapon);
  void (*ammo_set)(void *ctx, double weapon, double count);
} qa_q3_source_weapon_state;

typedef struct qa_q3_source_weapon_options {
  void *ctx;
  double msec; bool gauntlet_hit;
  qa_q3_external_weapon_slot *external_phase; /* NULL = TS undefined */
  double (*firing_delay)(void *ctx, double milliseconds); /* NULL absent */
  void (*event)(void *ctx, double event);
  void (*start_torso)(void *ctx, double animation);
} qa_q3_source_weapon_options;

/* TS Q3HoldableState is a Pick of the state above; same struct works. */
typedef qa_q3_source_weapon_state qa_q3_holdable_state;
bool qa_q3_step_holdable(qa_q3_holdable_state *state, bool pressed,
                         void (*event)(void *ctx, double event), void *ctx);
/* command carries only buttons/weapon (TS Pick). */
void qa_q3_run_weapon_step(qa_q3_source_weapon_state *state, qa_q3_move_command command,
                           qa_q3_source_weapon_options *options);
#endif
