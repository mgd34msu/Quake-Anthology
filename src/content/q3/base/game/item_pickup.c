/* Port of src/content/q3/base/game/item-pickup.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/item_pickup.h"
#include <math.h>
#include "qa/core/game_numeric.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
enum { RESPAWN_ARMOR = 25, RESPAWN_HEALTH = 35, RESPAWN_HOLDABLE = 60, RESPAWN_MEGAHEALTH = 35, RESPAWN_POWERUP = 120 };
enum { IT_BAD = 0, IT_WEAPON = 1, IT_AMMO = 2, IT_ARMOR = 3, IT_HEALTH = 4, IT_POWERUP = 5, IT_HOLDABLE = 6, IT_PERSISTANT_POWERUP = 7, IT_TEAM = 8 };
enum { GT_TEAM = 3 };
static const qa_q3_game_item_definition_mirror *lists[2]; static size_t list_counts[2];
void qa_q3_item_pickup_set_item_list_mirror(qa_q3_game_product p, const qa_q3_game_item_definition_mirror *items, size_t count) {
  lists[p] = items; list_counts[p] = count;
}
typedef struct { qa_q3_game_client *client; const qa_q3_game_item_definition_mirror *item; double item_index; } pickup_input;
static qa_q3_game_client *client_of(qa_q3_game_entity *e) { if (!e->client) qa_range_error("Item pickup requires a client entity"); return e->client; }
static pickup_input get_input(qa_q3_game_entity *item_entity, qa_q3_game_entity *other) {
  pickup_input in; in.client = client_of(other); in.item = item_entity->item;
  if (!in.item) qa_range_error("Item pickup requires an item definition");
  in.item_index = -1; const qa_q3_game_item_definition_mirror *list = lists[in.client->ps.product]; size_t n = list_counts[in.client->ps.product];
  for (size_t i = 0; i < n; i++) if (&list[i] == in.item) { in.item_index = (double)i; break; }
  if (in.item_index < 0) qa_range_error("Item does not belong to %s", qa_q3_game_product_name(in.client->ps.product));
  return in;
}
/* statSchema mirror: base weapons2 armor3 health0 holdable1 max6; missionpack weapons3 armor4 health0 holdable1 max7 pers2 */
static size_t stat_idx(qa_q3_game_product p, int which) {
  if (p == QA_Q3_PRODUCT_BASEQ3) { const size_t t[5] = {0, 1, 2, 3, 6}; return t[which]; }
  const size_t t[5] = {0, 1, 3, 4, 7}; return t[which];
}
static bool has_guard(qa_q3_game_client *c) {
  if (c->ps.product == QA_Q3_PRODUCT_BASEQ3) return false;
  double idx = qa_q3_game_slots_get_mirror(&c->ps.stats, 2);
  const qa_q3_game_item_definition_mirror *list = lists[QA_Q3_PRODUCT_MISSIONPACK];
  if (!list || idx < 0 || idx >= (double)list_counts[QA_Q3_PRODUCT_MISSIONPACK]) return false;
  return list[(size_t)idx].tag == 11;
}
static void add_ammo_to(qa_q3_game_client *c, double weapon, double count) {
  double total = qa_int32(qa_q3_game_slots_get_mirror(&c->ps.ammo, (size_t)weapon) + count);
  qa_q3_game_slots_set_mirror(&c->ps.ammo, (size_t)weapon, total > 200 ? 200 : total);
}
void qa_q3_add_ammo(qa_q3_game_entity *other, double weapon, double count) { add_ammo_to(client_of(other), weapon, count); }
static double pickup_ammo_from(qa_q3_game_entity *ie, qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item) {
  if (item->type != IT_AMMO) qa_range_error("Pickup_Ammo requires an ammo item");
  add_ammo_to(c, item->tag, ie->count != 0 ? ie->count : item->quantity); return QA_Q3_RESPAWN_AMMO;
}
double qa_q3_pickup_ammo(qa_q3_game_entity *ie, qa_q3_game_entity *other) { pickup_input in = get_input(ie, other); return pickup_ammo_from(ie, in.client, in.item); }
double qa_q3_weapon_pickup_quantity(qa_q3_weapon_pickup_quantity_input in) {
  if (in.count < 0) return 0;
  double q = in.count != 0 ? in.count : in.quantity;
  return !in.dropped && in.game_type != GT_TEAM ? (in.current_ammo < q ? q - in.current_ammo : 1) : q;
}
double qa_q3_weapon_respawn_seconds(qa_q3_weapon_pickup_context c) { return c.game_type == GT_TEAM ? c.team_weapon_respawn_seconds : c.weapon_respawn_seconds; }
double qa_q3_item_respawn_seconds(const qa_q3_game_item_definition_mirror *item, qa_q3_weapon_pickup_context c) {
  if (item->type == IT_WEAPON) return qa_q3_weapon_respawn_seconds(c);
  if (item->type == IT_AMMO) return QA_Q3_RESPAWN_AMMO;
  if (item->type == IT_ARMOR) return RESPAWN_ARMOR;
  if (item->type == IT_HEALTH) return item->quantity == 100 ? RESPAWN_MEGAHEALTH : RESPAWN_HEALTH;
  if (item->type == IT_HOLDABLE) return RESPAWN_HOLDABLE;
  if (item->type == IT_POWERUP) return RESPAWN_POWERUP;
  if (item->type == IT_PERSISTANT_POWERUP) return -1;
  if (item->type == IT_TEAM) qa_range_error("Team objective lifecycle requires its original pickup handler");
  qa_range_error("Invalid item has no pickup lifecycle");
}
static double pickup_weapon_from(qa_q3_game_entity *ie, qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item, qa_q3_weapon_pickup_context ctx) {
  if (item->type != IT_WEAPON) qa_range_error("Pickup_Weapon requires a weapon item");
  qa_q3_weapon_pickup_quantity_input qi = {ie->count, item->quantity, ((int)ie->flags & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0, ctx.game_type, qa_q3_game_slots_get_mirror(&c->ps.ammo, (size_t)item->tag)};
  double q = qa_q3_weapon_pickup_quantity(qi); size_t w = stat_idx(c->ps.product, 2);
  qa_q3_game_slots_set_mirror(&c->ps.stats, w, (int)qa_q3_game_slots_get_mirror(&c->ps.stats, w) | (1 << (int)item->tag));
  add_ammo_to(c, item->tag, q); if (item->tag == 10) qa_q3_game_slots_set_mirror(&c->ps.ammo, (size_t)item->tag, -1);
  return qa_q3_weapon_respawn_seconds(ctx);
}
double qa_q3_pickup_weapon(qa_q3_game_entity *ie, qa_q3_game_entity *other, qa_q3_weapon_pickup_context ctx) { pickup_input in = get_input(ie, other); return pickup_weapon_from(ie, in.client, in.item, ctx); }
static double pickup_health_from(qa_q3_game_entity *ie, qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item, qa_q3_game_entity *other) {
  if (item->type != IT_HEALTH) qa_range_error("Pickup_Health requires a health item");
  double max_h = qa_q3_game_slots_get_mirror(&c->ps.stats, stat_idx(c->ps.product, 4));
  double limit = has_guard(c) || (item->quantity != 5 && item->quantity != 100) ? max_h : max_h * 2;
  double q = ie->count != 0 ? ie->count : item->quantity;
  double h = qa_int32(qa_q3_game_entity_health(other) + q); if (h > limit) h = limit;
  qa_q3_game_entity_set_health(other, h); qa_q3_game_slots_set_mirror(&c->ps.stats, stat_idx(c->ps.product, 0), h);
  return item->quantity == 100 ? RESPAWN_MEGAHEALTH : RESPAWN_HEALTH;
}
double qa_q3_pickup_health(qa_q3_game_entity *ie, qa_q3_game_entity *other) { pickup_input in = get_input(ie, other); return pickup_health_from(ie, in.client, in.item, other); }
static double pickup_armor_from(qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item) {
  if (item->type != IT_ARMOR) qa_range_error("Pickup_Armor requires an armor item");
  double armor = qa_int32(qa_q3_game_slots_get_mirror(&c->ps.stats, stat_idx(c->ps.product, 3)) + item->quantity);
  double max_h = qa_q3_game_slots_get_mirror(&c->ps.stats, stat_idx(c->ps.product, 4));
  double limit = has_guard(c) ? max_h : max_h * 2;
  qa_q3_game_slots_set_mirror(&c->ps.stats, stat_idx(c->ps.product, 3), armor > limit ? limit : armor); return RESPAWN_ARMOR;
}
double qa_q3_pickup_armor(qa_q3_game_entity *ie, qa_q3_game_entity *other) { pickup_input in = get_input(ie, other); return pickup_armor_from(in.client, in.item); }
static double pickup_holdable_from(qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item, double idx) {
  if (item->type != IT_HOLDABLE) qa_range_error("Pickup_Holdable requires a holdable item");
  qa_q3_game_slots_set_mirror(&c->ps.stats, stat_idx(c->ps.product, 1), idx);
  if (item->tag == 3) c->ps.e_flags = (int)c->ps.e_flags | 0x200;
  return RESPAWN_HOLDABLE;
}
double qa_q3_pickup_holdable(qa_q3_game_entity *ie, qa_q3_game_entity *other) { pickup_input in = get_input(ie, other); return pickup_holdable_from(in.client, in.item, in.item_index); }
static double source_handicap(const char *raw) { double p = qa_game_atof(raw); return p <= 0 || p > 100 ? 100 : p; }
static double pickup_persistent_from(qa_q3_game_entity *ie, qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item, double idx, const char *raw, qa_q3_game_entity *other) {
  if (c->ps.product != QA_Q3_PRODUCT_MISSIONPACK) qa_range_error("Persistent powerups require missionpack player state");
  if (item->type != IT_PERSISTANT_POWERUP) qa_range_error("Pickup_PersistantPowerup requires a persistent powerup item");
  qa_q3_game_slots_set_mirror(&c->ps.stats, 2, idx); c->persistant_powerup = ie;
  double handicap = source_handicap(raw), player_max = trunc(handicap);
  if (item->tag == 11) {
    double maximum = trunc(2 * handicap); qa_q3_game_entity_set_health(other, maximum);
    qa_q3_game_slots_set_mirror(&c->ps.stats, 0, maximum); qa_q3_game_slots_set_mirror(&c->ps.stats, 7, maximum);
    qa_q3_game_slots_set_mirror(&c->ps.stats, 4, maximum); c->pers.max_health = maximum;
  } else if (item->tag == 10) { c->pers.max_health = player_max; qa_q3_game_slots_set_mirror(&c->ps.stats, 4, 0); }
  else if (item->tag == 13) { c->pers.max_health = player_max; for (size_t i = 0; i < c->ammo_times.length; i++) qa_q3_game_slots_set_mirror(&c->ammo_times, i, 0); }
  else c->pers.max_health = player_max;
  return -1;
}
double qa_q3_pickup_persistent_powerup(qa_q3_game_entity *ie, qa_q3_game_entity *other, const char *raw) { pickup_input in = get_input(ie, other); return pickup_persistent_from(ie, in.client, in.item, in.item_index, raw, other); }
static double pickup_powerup_from(qa_q3_game_entity *ie, qa_q3_game_client *c, const qa_q3_game_item_definition_mirror *item, qa_q3_powerup_pickup_context ctx) {
  if (item->type != IT_POWERUP) qa_range_error("Pickup_Powerup requires a powerup item");
  if (ctx.time != trunc(ctx.time) || ctx.time < -2147483648 || ctx.time > 2147483647) qa_range_error("Powerup pickup time must be a signed 32-bit millisecond value");
  double expiration = qa_q3_game_slots_get_mirror(&c->ps.powerups, (size_t)item->tag);
  if (expiration == 0) expiration = ctx.time - fmod(ctx.time, 1000);
  double q = ie->count != 0 ? ie->count : item->quantity;
  qa_q3_game_slots_set_mirror(&c->ps.powerups, (size_t)item->tag, qa_int32(expiration + q * 1000));
  for (size_t i = 0; i < ctx.client_count; i++) {
    qa_q3_game_client *cand = ctx.clients[i]; if (cand == c || cand->pers.connected == QA_Q3_CONN_DISCONNECTED) continue;
    if (qa_q3_game_slots_get_mirror(&cand->ps.stats, stat_idx(cand->ps.product, 0)) <= 0) continue;
    if (ctx.game_type >= GT_TEAM && cand->sess.session_team == c->sess.session_team) continue;
    qa_vec3 delta = qa_sub3(ie->s.pos.base, cand->ps.origin); if (qa_length3(delta) > 192) continue;
    if (qa_dot3(qa_normalize3(delta), qa_qvm_angle_vectors(cand->ps.viewangles).forward) < 0.4) continue;
    if (ctx.trace_solid_line(ctx.trace_ctx, cand->ps.origin, ie->s.pos.base).fraction != 1) continue;
    double ev = qa_q3_game_slots_get_mirror(&cand->ps.persistant, 5);
    qa_q3_game_slots_set_mirror(&cand->ps.persistant, 5, (int)ev ^ 0x0001);
  }
  return RESPAWN_POWERUP;
}
double qa_q3_pickup_powerup(qa_q3_game_entity *ie, qa_q3_game_entity *other, qa_q3_powerup_pickup_context ctx) { pickup_input in = get_input(ie, other); return pickup_powerup_from(ie, in.client, in.item, ctx); }
double qa_q3_pickup_item(qa_q3_game_entity *ie, qa_q3_game_entity *other, qa_q3_item_pickup_context ctx) {
  pickup_input in = get_input(ie, other);
  if (in.item->type == IT_WEAPON) return pickup_weapon_from(ie, in.client, in.item, ctx.weapon);
  if (in.item->type == IT_AMMO) return pickup_ammo_from(ie, in.client, in.item);
  if (in.item->type == IT_ARMOR) return pickup_armor_from(in.client, in.item);
  if (in.item->type == IT_HEALTH) return pickup_health_from(ie, in.client, in.item, other);
  if (in.item->type == IT_POWERUP) return pickup_powerup_from(ie, in.client, in.item, ctx.powerup);
  if (in.item->type == IT_HOLDABLE) return pickup_holdable_from(in.client, in.item, in.item_index);
  if (in.item->type == IT_PERSISTANT_POWERUP) return pickup_persistent_from(ie, in.client, in.item, in.item_index, ctx.handicap_for_client(ctx.handicap_ctx, in.client->ps.client_num), other);
  if (in.item->type == IT_TEAM) qa_range_error("Pickup_Item: IT_TEAM requires the team objective pickup handler");
  if (in.item->type == IT_BAD) qa_range_error("Pickup_Item: IT_BAD cannot be picked up");
  qa_range_error("Pickup_Item: unknown item");
}
