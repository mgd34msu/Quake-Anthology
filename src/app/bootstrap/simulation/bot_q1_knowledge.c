/* Port of src/app/bootstrap/simulation/bot-q1-knowledge.ts. WeaponInfo keeps
 * the computed numbers unrounded, as the TS WeaponInfo object does.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_q1_knowledge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/content/q1/foundation/extensions.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#define JS_PI 3.141592653589793 /* Math.PI */

typedef struct shot_values {
  double damage, count, cycle, ammo, speed, range, radius, spread_x, spread_y, forward, side;
} shot_values;
typedef struct entry { qa_q1_base_weapon weapon; int64_t slot; qa_item_id item; qa_item_id ammo; } entry;
typedef struct q1_knowledge {
  qa_q1_services *game;
  entry *entries; size_t entry_count;
  qa_q1_weapon *weapons; /* the combined weapon list (uncovered entries borrow it) */
} q1_knowledge;

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static shot_values row(double damage, double count, double cycle, double ammo, double speed, double range, double radius,
    double spread_x, double spread_y, double forward, double side) {
  shot_values s = {damage, count, cycle, ammo, speed, range, radius, spread_x, spread_y, forward, side};
  return s;
}
/* progs106 weapons.qc/player.qc and foundation/weapons.ts; continuous frames fire every 0.1 seconds. */
static shot_values shot(qa_q1_services *game, qa_q1_base_weapon weapon, qa_q1_player_state *player) {
  double nails = player == NULL ? 2 : qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/nails");
  double shells = player == NULL ? 2 : qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/shells");
  double nail_speed = player == NULL ? 1000 : qa_q1_nail_speed(game, player, 1000);
  double nail_side = player != NULL ? player->nail_side : 1;
  if (is(weapon, "axe")) return row(20, 1, 0.5, 0, 0, 64, 0, 0, 0, 0, 0);
  if (is(weapon, "shotgun")) return row(4, 6, 0.5, 1, 0, 2048, 0, 0.04, 0.04, 10, 0);
  if (is(weapon, "supershotgun")) return shells > 1 ? row(4, 14, 0.7, 2, 0, 2048, 0, 0.14, 0.08, 10, 0) : row(4, 6, 0.7, 1, 0, 2048, 0, 0.04, 0.04, 10, 0);
  if (is(weapon, "nailgun")) return row(9, 1, 0.1, 1, nail_speed, nail_speed * 6, 0, 0, 0, 0, nail_side * 4);
  if (is(weapon, "supernailgun")) return nails >= 2 ? row(18, 1, 0.1, 2, nail_speed, nail_speed * 6, 0, 0, 0, 0, 0)
      : row(9, 1, 0.1, 1, nail_speed, nail_speed * 6, 0, 0, 0, 0, nail_side * 4);
  if (is(weapon, "rocketlauncher")) return row(110, 1, 0.8, 1, 1000, 5000, 160, 0, 0, 8, 0);
  if (is(weapon, "lightning")) return row(30, 1, 0.1, 1, 0, 600, 0, 0, 0, 0, 0);
  /* grenadelauncher; the TS switch is exhaustive over Q1BaseWeapon. */
  qa_raise("Error", 0, 0, "Q1 grenade trajectories are not admitted by bot weapon knowledge");
}
static bool registered(const qa_q1_services *game, qa_q1_weapon weapon) {
  for (size_t i = 0; i < game->registered_weapon_count; i++) if (is(game->registered_weapons[i].def->id, weapon)) return true;
  return false;
}
static bool usable(qa_app_sim_bot_arsenal *a, qa_actor_id actor, const entry *e) {
  q1_knowledge *k = a->impl;
  qa_q1_player_state *player = qa_q1_player(k->game, actor);
  return player != NULL && qa_q1_weapon_available(k->game, player, e->weapon, NULL);
}
static void inventory_set(qa_bot_state_bots_a *state, int64_t index, double value) {
  if (index >= 0 && index < QA_Q3_BASE_MAX_ITEMS) state->src->inventory[index] = qa_js_to_int32(value); /* Int32Array store */
}
static void update_inventory(void *ctx, qa_bot_state_bots_a *state) {
  qa_app_sim_bot_arsenal *a = ctx;
  q1_knowledge *k = a->impl;
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_actor(a, state->src->client, &actor);
  qa_app_sim_bot_arsenal_track(a, state->src->ws, has_actor ? &actor : NULL);
  for (size_t index = 0; index < 200; index++) state->src->inventory[index] = 0;
  qa_combat_state_result combat = {0};
  if (has_actor) combat = qa_gameplay_authority_read(a->simulation->combat, actor);
  inventory_set(state, QA_INV_HEALTH, combat.has_value ? combat.value.health : 0);
  inventory_set(state, QA_INV_ARMOR, !combat.has_value || combat.value.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : combat.value.armor.regular.points);
  inventory_set(state, QA_INV_QUAD, (double)(has_actor && qa_q1_powerup_expires(k->game, actor, "quad") > k->game->time));
  for (size_t i = 0; i < k->entry_count; i++) {
    const entry *e = &k->entries[i];
    inventory_set(state, 64 + e->slot, !has_actor ? 0 : (double)usable(a, actor, e));
    inventory_set(state, 96 + e->slot, !has_actor || e->ammo == NULL ? 0 : qa_shared_inventory_table_count(a->simulation->inventory, actor, e->ammo));
  }
}
static qa_bot_weapon_knowledge_bots_a *candidates(void *ctx, qa_bot_library_bots_a *library, int64_t handle, size_t *out_count) {
  (void)library; /* TS (_library, handle) */
  qa_app_sim_bot_arsenal *a = ctx;
  q1_knowledge *k = a->impl;
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_handle_actor(a, handle, &actor);
  qa_q1_player_state *player = has_actor ? qa_q1_player(k->game, actor) : NULL;
  qa_body_state_result body = {0};
  if (has_actor) body = qa_shared_body_table_read(a->simulation->bodies, actor);
  qa_bot_weapon_knowledge_bots_a *out = calloc(k->entry_count != 0 ? k->entry_count : 1, sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < k->entry_count; i++) {
    const entry *e = &k->entries[i];
    shot_values values = shot(k->game, e->weapon, player);
    bool bullets = is(e->weapon, "shotgun") || is(e->weapon, "supershotgun");
    double muzzle_height = bullets && body.has_value
        ? body.value.bounds.min.z + (body.value.bounds.max.z - body.value.bounds.min.z) * 0.7 : bullets ? 15.2 : 16;
    qa_weapon_info_bots_a *info = &out[i].info;
    info->valid = true; info->number = (int32_t)e->slot;
    snprintf(info->name, sizeof info->name, "%s", e->weapon);
    const char *model = qa_q1_services_weapon_model(k->game, e->weapon, NULL);
    snprintf(info->model, sizeof info->model, "%s", model != NULL ? model : "");
    info->weapon_inventory_index = (int32_t)(64 + e->slot);
    snprintf(info->projectile, sizeof info->projectile, "%s", e->weapon);
    info->projectile_count = qa_js_to_int32(values.count);
    info->horizontal_spread = atan(values.spread_x) * 180 / JS_PI / 6;
    info->vertical_spread = atan(values.spread_y) * 180 / JS_PI / 6;
    info->speed = values.speed;
    info->offset = (qa_vec3){values.forward, values.side, muzzle_height - 22};
    info->ammo_amount = qa_js_to_int32(values.ammo); info->ammo_inventory_index = (int32_t)(96 + e->slot);
    info->reload = values.cycle;
    snprintf(info->projectile_info.name, sizeof info->projectile_info.name, "%s", e->weapon);
    info->projectile_info.damage = values.damage;
    info->projectile_info.radius = values.radius;
    info->projectile_info.damage_type = QA_DAMAGE_TYPE_IMPACT | (values.radius > 0 ? QA_DAMAGE_TYPE_RADIAL : 0);
    out[i].has_maximum_range = true; out[i].maximum_range = values.range;
    out[i].melee = is(e->weapon, "axe"); out[i].has_personality_role = false;
    out[i].has_supply = true;
    out[i].supply.weapon = e->item;
    out[i].supply.owned = has_actor && qa_shared_inventory_table_count(a->simulation->inventory, actor, e->item) > 0;
    out[i].supply.has_ammo = e->ammo != NULL; out[i].supply.ammo_item = e->ammo; out[i].supply.per_shot = values.ammo;
  }
  *out_count = k->entry_count;
  return out;
}
static const entry *entry_for(const q1_knowledge *k, int64_t slot) {
  for (size_t i = 0; i < k->entry_count; i++) if (k->entries[i].slot == slot) return &k->entries[i];
  return NULL;
}
static qa_item_id resolve_weapon(qa_app_sim_bot_arsenal *a, int64_t client, int64_t decision_slot) {
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_actor(a, client, &actor);
  const entry *e = entry_for(a->impl, decision_slot);
  return has_actor && e != NULL && usable(a, actor, e) ? e->item : NULL;
}
static int64_t source_weapon_state(qa_app_sim_bot_arsenal *a, int64_t client) {
  q1_knowledge *k = a->impl;
  qa_actor_id actor;
  if (!qa_app_sim_bot_arsenal_actor(a, client, &actor)) return QA_Q3_WEAPON_READY;
  qa_q1_player_state *player = qa_q1_player(k->game, actor);
  return (player != NULL ? player->attack_finished : 0) > k->game->time ? QA_Q3_WEAPON_FIRING : QA_Q3_WEAPON_READY;
}
static int64_t source_weapon(qa_app_sim_bot_arsenal *a, int64_t client) {
  q1_knowledge *k = a->impl;
  qa_actor_id actor;
  qa_q1_player_state *player = qa_app_sim_bot_arsenal_actor(a, client, &actor) ? qa_q1_player(k->game, actor) : NULL;
  const char *current = player != NULL ? player->weapon : NULL;
  for (size_t i = 0; i < k->entry_count; i++) if (current != NULL && is(k->entries[i].weapon, current)) return k->entries[i].slot;
  return 0;
}
static void destroy_impl(void *impl) { q1_knowledge *k = impl; free(k->entries); free(k->weapons); free(k); }

qa_app_sim_bot_arsenal *qa_app_sim_create_q1_bot_knowledge(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx) {
  qa_q1_services *game = simulation->q1_weapon_source(simulation->ctx);
  if (game == NULL) qa_raise("Error", 0, 0, "Q1 bot weapon knowledge requires a selected Q1 arsenal");
  q1_knowledge *k = calloc(1, sizeof *k);
  if (k == NULL) qa_range_error("Out of memory");
  k->game = game;
  /* [...new Set<Q1Weapon>([...WEAPONS, ...game.registeredWeapons.keys()])] */
  size_t total = 8 + game->registered_weapon_count, count = 0;
  k->weapons = calloc(total, sizeof *k->weapons);
  k->entries = calloc(total, sizeof *k->entries);
  qa_app_sim_bot_arsenal *a = qa_app_sim_bot_arsenal_alloc("q1", simulation, actor_for_client, actor_ctx);
  a->uncovered_weapons = calloc(total, sizeof *a->uncovered_weapons);
  if (k->weapons == NULL || k->entries == NULL || a->uncovered_weapons == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < total; i++) {
    qa_q1_weapon weapon = i < 8 ? QA_Q1_WEAPONS[i] : game->registered_weapons[i - 8].def->id;
    bool seen = false;
    for (size_t j = 0; j < count && !seen; j++) seen = is(k->weapons[j], weapon);
    if (!seen) k->weapons[count++] = weapon;
  }
  for (size_t index = 0; index < count; index++) {
    qa_q1_weapon weapon = k->weapons[index];
    if (!qa_q1_is_base_weapon(weapon) || registered(game, weapon) || is(weapon, "grenadelauncher")) {
      a->uncovered_weapons[a->uncovered_count++] = weapon; continue;
    }
    entry *e = &k->entries[k->entry_count++];
    e->weapon = weapon; e->slot = (int64_t)index + 1;
    e->item = qa_q1_services_weapon_item(game, weapon); e->ammo = qa_q1_services_weapon_ammo(game, weapon);
  }
  a->impl = k; a->destroy_impl = destroy_impl;
  a->resolve_weapon = resolve_weapon; a->source_weapon = source_weapon; a->source_weapon_state = source_weapon_state;
  qa_app_sim_bot_arsenal_bind(a, update_inventory, candidates);
  return a;
}
