/* Port of src/app/bootstrap/simulation/q3/guest-player.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_player.h"
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/app/bootstrap/simulation/powerup_timers.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

typedef struct timer_ctx { qa_q3_player_state state; } timer_ctx;
static double timer_expires(void *v, double powerup) {
  timer_ctx *ctx = (timer_ctx *)v;
  size_t i = (size_t)powerup;
  return powerup >= 0 && i < ctx->state.powerups.count ? ctx->state.powerups.values[i] : 0;
}
static size_t powerup_timers(qa_q3_player_state state, double now, qa_active_powerup_timer *out, size_t capacity) {
  timer_ctx ctx = {state};
  return qa_app_sim_q3_public_powerup_timers(timer_expires, &ctx, now, out, capacity);
}
static double view_at(qa_double_view view, double index) {
  size_t i = (size_t)index;
  return index >= 0 && i < view.count ? view.values[i] : 0;
}
static bool item_eq(qa_item_id a, qa_item_id b) { return a && b && strcmp(a, b) == 0; }

typedef struct count_ctx {
  qa_q3_player_state state;
  const qa_q3_guest_weapon *definitions;
  size_t definition_count;
  const qa_q3_base_stat_schema *stats;
  const qa_inventory_entry *inventory;
  size_t inventory_count;
  bool has_inventory;
} count_ctx;
static double count_item(count_ctx *ctx, qa_item_id item) {
  if (ctx->has_inventory) {
    for (size_t i = 0; i < ctx->inventory_count; i++) if (item_eq(ctx->inventory[i].item, item)) return ctx->inventory[i].count;
    return 0;
  }
  const qa_q3_guest_weapon *definition = NULL;
  for (size_t i = 0; i < ctx->definition_count; i++)
    if (item_eq(ctx->definitions[i].item, item) || (ctx->definitions[i].ammo && item_eq(ctx->definitions[i].ammo, item))) definition = &ctx->definitions[i];
  if (!definition) return 0;
  if (item_eq(definition->item, item))
    return ((qa_js_to_int32(view_at(ctx->state.stats, ctx->stats->weapons))) & qa_js_to_int32(qa_js_shl(1, definition->weapon))) != 0 ? 1 : 0;
  return view_at(ctx->state.ammo, definition->weapon);
}
static double count_cb(void *v, qa_item_id item) { return count_item((count_ctx *)v, item); }

qa_app_guest_player_ui qa_app_q3_guest_player_ui(qa_q3_player_state state, qa_provider_reference source,
    double now_milliseconds, const qa_q3_guest_weapon *catalog, size_t catalog_count, const char *product,
    const qa_inventory_entry *inventory, size_t inventory_count, bool has_inventory,
    qa_item_id active, bool has_active, qa_app_guest_player_ui out) {
  bool missionpack = product && strcmp(product, "missionpack") == 0;
  bool standard = catalog == NULL;
  const qa_q3_guest_weapon *definitions = catalog ? catalog
      : missionpack ? qa_q3_standard_guest_weapons : qa_q3_base_guest_weapons;
  size_t definition_count = catalog ? catalog_count
      : missionpack ? QA_Q3_STANDARD_GUEST_WEAPON_COUNT : QA_Q3_BASE_GUEST_WEAPON_COUNT;
  if (catalog == qa_q3_standard_guest_weapons || catalog == qa_q3_base_guest_weapons) standard = true;
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(missionpack ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3);
  const qa_q3_guest_weapon *weapon = NULL;
  /* TS definitions.find: the first match. A weapon missing from the catalog shows no active weapon. */
  for (size_t i = 0; i < definition_count && weapon == NULL; i++) {
    if (!has_active && definitions[i].weapon == state.weapon) weapon = &definitions[i];
    if (has_active && active && item_eq(definitions[i].item, active)) weapon = &definitions[i];
  }
  count_ctx ctx = {state, definitions, definition_count, stats, inventory, inventory_count, has_inventory};
  double armor = view_at(state.stats, stats->armor);
  out.powerup_count = powerup_timers(state, now_milliseconds, out.powerups, out.powerup_capacity);
  out.health = view_at(state.stats, stats->health);
  memset(&out.armor, 0, sizeof out.armor);
  out.armor.powered.kind = "none";
  if (armor == 0) out.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  else {
    out.armor.regular.kind = QA_REGULAR_ARMOR_Q3;
    out.armor.regular.points = armor;
    out.armor.regular.protection = (double)(float)0.66;
  }
  out.active_weapon = weapon ? weapon->item : NULL;
  out.has_ammo = weapon && weapon->ammo;
  if (out.has_ammo) { out.ammo_item = weapon->ammo; out.ammo_count = count_item(&ctx, weapon->ammo); }
  if (has_inventory) {
    out.inventory_count = inventory_count < out.inventory_capacity ? inventory_count : out.inventory_capacity;
    if (out.inventory_count) memcpy(out.inventory, inventory, out.inventory_count * sizeof *out.inventory);
  } else {
    out.inventory_count = 0;
    for (size_t i = 0; i < definition_count; i++) {
      if (out.inventory_count < out.inventory_capacity) {
        qa_inventory_entry *row = &out.inventory[out.inventory_count++];
        memset(row, 0, sizeof *row);
        row->item = definitions[i].item; row->count = count_item(&ctx, definitions[i].item); row->capacity = 1;
      }
      if (definitions[i].ammo && out.inventory_count < out.inventory_capacity) {
        qa_inventory_entry *row = &out.inventory[out.inventory_count++];
        memset(row, 0, sizeof *row);
        row->item = definitions[i].ammo; row->count = count_item(&ctx, definitions[i].ammo); row->capacity = 200;
      }
    }
  }
  out.has_weapon_status = weapon != NULL;
  if (weapon) {
    memset(&out.weapon_status, 0, sizeof out.weapon_status);
    out.weapon_status.source = source; out.weapon_status.item = weapon->item; out.weapon_status.label = weapon->label;
    if (weapon->ammo && count_item(&ctx, weapon->ammo) != -1) {
      out.weapon_status.ammo_is_finite = true; out.weapon_status.ammo_item = weapon->ammo;
      out.weapon_status.ammo_count = count_item(&ctx, weapon->ammo);
      out.weapon_status.has_ammo_to_start = out.weapon_status.ammo_count > 0;
      out.weapon_status.low = false;
    }
  }
  out.arsenal_warning = standard ? qa_app_q3_arsenal_warning(product ? product : "baseq3", count_cb, &ctx) : "none";
  out.item_count = 0;
  for (size_t i = 0; i < definition_count && out.item_count < out.item_capacity; i++) {
    qa_app_player_ui_item *row = &out.items[out.item_count++];
    memset(row, 0, sizeof *row);
    row->id = definitions[i].item; row->label = definitions[i].label; row->kind = "weapon";
    row->source_ordinal = definitions[i].weapon;
    row->owned = count_item(&ctx, definitions[i].item) > 0;
    row->has_ammo = definitions[i].ammo == NULL || count_item(&ctx, definitions[i].ammo) != 0;
    row->has_count = definitions[i].ammo != NULL;
    row->count = definitions[i].ammo ? count_item(&ctx, definitions[i].ammo) : 0;
    row->warning_count = 0;
  }
  return out;
}
