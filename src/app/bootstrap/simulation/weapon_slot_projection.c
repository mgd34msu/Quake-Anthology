/* Port of src/app/bootstrap/simulation/weapon-slot-projection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/weapon_slot_projection.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
qa_app_sim_weapon_slot_projection qa_app_sim_project_weapon_slot(qa_app_sim_weapon_slot_state state,
    qa_app_sim_primary_weapon_projection primary, const qa_app_sim_equipment_weapon_projection *equipment,
    const qa_app_sim_source_weapon_projection *sources, size_t source_count) {
  /* Equipment becomes an extra source projection (weaponStatus unmetered). */
  qa_weapon_hud_status equip_status; memset(&equip_status, 0, sizeof equip_status);
  qa_app_sim_source_weapon_projection equip_proj; memset(&equip_proj, 0, sizeof equip_proj);
  /* Supplied items map (id -> item), sources first then equipment appended. */
  size_t supplied_cap = 0;
  for (size_t i = 0; i < source_count; i++) supplied_cap += sources[i].item_count;
  if (equipment) supplied_cap += 1;
  qa_app_sim_player_ui_item *supplied = supplied_cap ? malloc(supplied_cap * sizeof *supplied) : NULL;
  if (supplied_cap && !supplied) qa_range_error("Out of memory");
  size_t supplied_count = 0;
  for (size_t i = 0; i < source_count; i++)
    for (size_t j = 0; j < sources[i].item_count; j++) {
      /* Map set replaces same key in place (insertion order kept). */
      size_t at = supplied_count;
      for (size_t k = 0; k < supplied_count; k++) if (is(supplied[k].id, sources[i].items[j].id)) at = k;
      supplied[at] = sources[i].items[j]; if (at == supplied_count) supplied_count++;
    }
  if (equipment) {
    equip_status.source = equipment->source; equip_status.item = equipment->weapon.item;
    equip_status.label = equipment->item.label; equip_status.ammo_is_finite = false; /* TS ammo {kind:"unmetered"} */
    equip_proj.provider = equipment->weapon.provider; equip_proj.active = &equipment->weapon;
    equip_proj.items = &equipment->item; equip_proj.item_count = 1; equip_proj.model = equipment->model;
    equip_proj.weapon_status = &equip_status;
    size_t at = supplied_count;
    for (size_t k = 0; k < supplied_count; k++) if (is(supplied[k].id, equipment->item.id)) at = k;
    supplied[at] = equipment->item; if (at == supplied_count) supplied_count++;
  }
  qa_provider_id provider = is(state.kind, "active") ? state.provider : state.from;
  const qa_app_sim_source_weapon_projection *visible = NULL;
  for (size_t i = 0; i < source_count && !visible; i++) if (is(sources[i].provider, provider)) visible = &sources[i];
  if (!visible && equipment && is(equip_proj.provider, provider)) visible = &equip_proj;
  /* items = primary items not supplied, then supplied values. */
  size_t cap = primary.ui.item_count + supplied_count;
  qa_app_sim_player_ui_item *items = cap ? malloc(cap * sizeof *items) : NULL;
  if (cap && !items) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < primary.ui.item_count; i++) {
    bool has = false;
    for (size_t k = 0; k < supplied_count; k++) if (is(supplied[k].id, primary.ui.items[i].id)) has = true;
    if (!has) items[count++] = primary.ui.items[i];
  }
  for (size_t k = 0; k < supplied_count; k++) items[count++] = supplied[k];
  free(supplied);
  qa_app_sim_weapon_slot_projection out;
  out.active = visible ? visible->active : primary.active;
  static qa_weapon_reference next_ref;
  if (!is(state.kind, "active")) { next_ref = state.next; out.pending = &next_ref; }
  else out.pending = visible ? visible->pending : primary.pending;
  out.ui = primary.ui; out.ui.items = items; out.ui.item_count = count;
  out.ui.weapon_status = visible ? visible->weapon_status : primary.ui.weapon_status;
  out.ui.active_weapon = visible ? (visible->active ? visible->active->item : NULL) : primary.ui.active_weapon;
  if (visible) { out.ui.has_ammo = visible->has_ammo; out.ui.ammo_item = visible->ammo_item; out.ui.ammo_count = visible->ammo_count; }
  out.model = visible ? visible->model : primary.model;
  return out;
}
