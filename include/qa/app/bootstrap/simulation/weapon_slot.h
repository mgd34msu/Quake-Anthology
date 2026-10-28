/* Port of src/app/bootstrap/simulation/weapon-slot.ts.
 * PrimaryWeaponHandoff/WeaponReference are the landed
 * qa_source_weapon_handoff/qa_weapon_reference. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_WEAPON_SLOT_H
#define QA_APP_BOOTSTRAP_SIMULATION_WEAPON_SLOT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/source_items.h"
typedef qa_source_weapon_handoff qa_app_sim_primary_weapon_handoff;
typedef struct qa_app_sim_equipment_weapon_handoff {
  void *ctx;
  qa_weapon_reference weapon;
  void (*holster)(void *ctx);
  bool (*is_holstered)(void *ctx);
  void (*resume)(void *ctx);
} qa_app_sim_equipment_weapon_handoff;
/* kind: "active"|"switching"|"activating"|"primary"|"equipment"|
 * "holstering-primary"|"holstering-equipment" (restore adds the last 4). */
typedef struct qa_app_sim_weapon_slot_state {
  const char *kind;
  qa_provider_id provider; /* active */
  qa_provider_id from;     /* switching/activating */
  qa_weapon_reference next;
  double request;          /* activating */
} qa_app_sim_weapon_slot_state;
typedef qa_app_sim_weapon_slot_state qa_app_sim_weapon_slot_restore_state;
typedef struct qa_app_sim_weapon_slot qa_app_sim_weapon_slot;
/* equipment may be NULL; restored may be NULL; actor_current may be NULL (always true). */
qa_app_sim_weapon_slot *qa_app_sim_weapon_slot_create(qa_source_weapon_handoff primary,
    const qa_app_sim_equipment_weapon_handoff *equipment,
    const qa_app_sim_weapon_slot_restore_state *restored,
    bool (*actor_current)(void *ctx), void *actor_current_ctx);
void qa_app_sim_weapon_slot_destroy(qa_app_sim_weapon_slot *slot);
qa_app_sim_weapon_slot_state qa_app_sim_weapon_slot_snapshot(const qa_app_sim_weapon_slot *slot);
void qa_app_sim_weapon_slot_validate_restore(qa_app_sim_weapon_slot *slot);
bool qa_app_sim_weapon_slot_selected(qa_app_sim_weapon_slot *slot, qa_provider_id provider);
bool qa_app_sim_weapon_slot_presented(qa_app_sim_weapon_slot *slot, qa_provider_id provider);
bool qa_app_sim_weapon_slot_primary_selected(qa_app_sim_weapon_slot *slot);
bool qa_app_sim_weapon_slot_equipment_selected(qa_app_sim_weapon_slot *slot);
/* Returns an unbind handle owned by the slot; invoke via unbind(). */
int qa_app_sim_weapon_slot_bind(qa_app_sim_weapon_slot *slot, qa_source_weapon_binding source);
void qa_app_sim_weapon_slot_unbind(qa_app_sim_weapon_slot *slot, int handle);
/* Borrowed array owned by slot, valid until next call. */
const qa_source_weapon_presentation *qa_app_sim_weapon_slot_presentations(qa_app_sim_weapon_slot *slot, size_t *out_count);
bool qa_app_sim_weapon_slot_request(qa_app_sim_weapon_slot *slot, qa_weapon_reference weapon);
void qa_app_sim_weapon_slot_reconcile(qa_app_sim_weapon_slot *slot);
#endif
