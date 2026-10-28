/* Port of src/app/bootstrap/simulation/quakec-player-ui.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_player_ui.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
qa_app_sim_quakec_weapon_ui qa_app_sim_quakec_weapon_ui_of(double items, double weapon, double current_ammo,
    const qa_app_sim_quakec_weapon_ui_binding *bindings, size_t binding_count) {
  const qa_app_sim_quakec_weapon_ui_binding *active = NULL;
  for (size_t i = 0; i < binding_count; i++) if (bindings[i].bit == weapon) { active = &bindings[i]; break; }
  static const struct { int bit; qa_item_id item; } ammunition[] = {
    {256, "q1:ammo/shells"}, {512, "q1:ammo/nails"}, {1024, "q1:ammo/rockets"}, {2048, "q1:ammo/cells"},
  };
  qa_item_id ammo_item = NULL;
  for (size_t i = 0; i < sizeof ammunition / sizeof ammunition[0]; i++)
    if (((int)items & ammunition[i].bit) != 0) { ammo_item = ammunition[i].item; break; }
  qa_app_sim_quakec_weapon_ui out; memset(&out, 0, sizeof out);
  out.active_weapon = active != NULL ? active->item : NULL;
  out.has_ammo = ammo_item != NULL; out.ammo_item = ammo_item; out.ammo_count = current_ammo;
  out.item_count = binding_count;
  out.items = binding_count ? malloc(binding_count * sizeof *out.items) : NULL;
  if (binding_count && out.items == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < binding_count; i++) {
    const qa_app_sim_quakec_weapon_ui_binding *b = &bindings[i];
    qa_app_sim_player_ui_item *item = &out.items[i]; memset(item, 0, sizeof *item);
    item->id = b->item; item->label = b->label; item->kind = "weapon"; item->source_ordinal = b->impulse;
    item->owned = (((int)items & (int)b->bit) != 0);
    item->has_ammo = b != active || ammo_item == NULL || current_ammo > 0;
    item->has_count = b == active && ammo_item != NULL;
    item->count = item->has_count ? current_ammo : 0;
    item->warning_count = 0;
  }
  return out;
}
