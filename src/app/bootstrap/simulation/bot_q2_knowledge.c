/* Port of src/app/bootstrap/simulation/bot-q2-knowledge.ts. WeaponInfo keeps
 * the computed numbers unrounded (109.5 rocket damage, atan spreads), as the
 * TS WeaponInfo object does. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_q2_knowledge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#define JS_PI 3.141592653589793 /* Math.PI */

typedef struct ballistics {
  double damage, speed, range, radius, horizontal, vertical, count, cycle, gravity, bounce, detonation, lift;
} ballistics;
typedef struct entry { int64_t slot; const qa_q2w_definition *definition; ballistics shot; qa_weapon_info_bots_a info; } entry;
typedef struct q2_knowledge {
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  entry *entries; size_t entry_count;
} q2_knowledge;

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static ballistics shot(double damage, double speed, double range, double radius, double horizontal, double vertical, double count, double cycle) {
  ballistics b = {damage, speed, range, radius, horizontal, vertical, count, cycle, 0, 0, 0, 0};
  return b;
}
/* p_weapon callbacks provide these unpowered shot values; random damage uses its arithmetic mean. */
static bool weapon_ballistics(const char *name, bool rerelease, bool deathmatch, ballistics *out) {
  if (is(name, "blaster")) *out = shot(rerelease || deathmatch ? 15 : 10, rerelease ? 1500 : 1000, rerelease ? 3000 : 2000, 0, 0, 0, 1, 0.5);
  else if (is(name, "shotgun")) *out = shot(4, 0, 8192, 0, 500, 500, 12, 1.2);
  else if (is(name, "supershotgun")) *out = shot(6, 0, 8192, 0, 1000, 500, 20, 1.2);
  else if (is(name, "machinegun")) *out = shot(8, 0, 8192, 0, 300, 500, 1, 0.1);
  else if (is(name, "chaingun")) *out = shot(deathmatch ? 6 : 8, 0, 8192, 0, 300, 500, 3, 0.1);
  else if (is(name, "grenadelauncher")) {
    *out = shot(120, 600, 1500, 160, 0, 0, 1, 1.2);
    out->gravity = 1; out->bounce = 1.5; out->detonation = 2.5; out->lift = 200;
  } else if (is(name, "rocketlauncher")) *out = shot(109.5, 650, 8000, 120, 0, 0, 1, 0.9);
  else if (is(name, "hyperblaster")) *out = shot(deathmatch ? 15 : 20, 1000, 2000, 0, 0, 0, 1, 0.1);
  else if (is(name, "railgun")) *out = shot(deathmatch ? 100 : rerelease ? 125 : 150, 0, 8192, 0, 0, 0, 1, 1.6);
  else if (is(name, "ionripper")) *out = shot(deathmatch ? 30 : 50, 500, 1500, 0, tan(JS_PI / 180) * 8192, 0, 1, 0.3);
  else if (is(name, "phalanx")) *out = shot(74.5, 725, 8000, 120, tan(1.5 * JS_PI / 180) * 8192, 0, 2, 1.6);
  else if (is(name, "etf_rifle")) *out = shot(10, rerelease ? 1150 : 750, 8000, 0, 0, 0, 1, 0.1);
  else if (is(name, "heatbeam")) *out = shot(15, 0, 8192, 0, 0, 0, 1, 0.1);
  else return false;
  return true;
}
static qa_weapon_info_bots_a weapon_info(const qa_q2w_definition *definition, int64_t slot, ballistics s, bool rerelease) {
  const char *name = definition->name;
  qa_vec3 offset;
  if (is(name, "blaster") || is(name, "hyperblaster")) offset = (qa_vec3){24, 8, -8};
  else if (is(name, "ionripper")) offset = (qa_vec3){16, 7, -8};
  else if (is(name, "etf_rifle")) offset = (qa_vec3){15, 8, -8};
  else if (is(name, "heatbeam")) offset = (qa_vec3){7, 2, -3};
  else if (is(name, "railgun")) offset = (qa_vec3){0, 7, -8};
  else if (is(name, "rocketlauncher")) offset = (qa_vec3){8, 8, -8};
  else if (is(name, "grenadelauncher")) offset = (qa_vec3){8, rerelease ? 0 : 8, -8};
  else offset = (qa_vec3){0, is(name, "phalanx") ? 8 : rerelease ? 0 : 8, -8};
  qa_weapon_info_bots_a info;
  memset(&info, 0, sizeof info);
  info.valid = true; info.number = (int32_t)slot;
  snprintf(info.name, sizeof info.name, "%s", name);
  snprintf(info.model, sizeof info.model, "%s", definition->view_model != NULL ? definition->view_model : "");
  info.level = 0; info.weapon_inventory_index = (int32_t)(64 + slot); info.flags = 0;
  snprintf(info.projectile, sizeof info.projectile, "%s", name);
  info.projectile_count = qa_js_to_int32(s.count);
  info.horizontal_spread = atan(s.horizontal / 8192) * 180 / JS_PI / 6;
  info.vertical_spread = atan(s.vertical / 8192) * 180 / JS_PI / 6;
  info.speed = s.speed; info.acceleration = 0;
  info.offset = offset; /* recoil, angleOffset: zero */
  info.extra_z_velocity = s.lift;
  info.ammo_amount = qa_js_to_int32(definition->quantity); info.ammo_inventory_index = (int32_t)(96 + slot);
  info.activate = definition->activate_last * 0.1; info.reload = s.cycle;
  info.spin_up = is(name, "chaingun") ? 1 : 0; info.spin_down = 0;
  snprintf(info.projectile_info.name, sizeof info.projectile_info.name, "%s", name);
  info.projectile_info.gravity = s.gravity;
  info.projectile_info.damage = s.damage;
  info.projectile_info.radius = s.radius;
  info.projectile_info.damage_type = QA_DAMAGE_TYPE_IMPACT | (s.radius > 0 ? QA_DAMAGE_TYPE_RADIAL : 0);
  info.projectile_info.detonation = s.detonation; info.projectile_info.bounce = s.bounce;
  return info;
}
static void inventory_set(qa_bot_state_bots_a *state, int64_t index, double value) {
  if (index >= 0 && index < QA_Q3_BASE_MAX_ITEMS) state->src->inventory[index] = qa_js_to_int32(value); /* Int32Array store */
}
static double count(qa_app_sim_bot_arsenal *a, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(a->simulation->inventory, actor, item);
}
static bool can_use(qa_app_sim_bot_arsenal *a, qa_actor_id actor, const entry *e) {
  return count(a, actor, e->definition->item) > 0
      && (e->definition->ammo == NULL || count(a, actor, e->definition->ammo) >= e->definition->quantity);
}
static void update_inventory(void *ctx, qa_bot_state_bots_a *state) {
  qa_app_sim_bot_arsenal *a = ctx;
  q2_knowledge *k = a->impl;
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_actor(a, state->src->client, &actor);
  qa_combat_state_result combat = {0};
  if (has_actor) combat = qa_gameplay_authority_read(a->simulation->combat, actor);
  qa_app_sim_bot_arsenal_track(a, state->src->ws, has_actor ? &actor : NULL);
  for (size_t index = 0; index < 200; index++) state->src->inventory[index] = 0;
  inventory_set(state, QA_INV_HEALTH, combat.has_value ? combat.value.health : 0);
  inventory_set(state, QA_INV_ARMOR, !combat.has_value || combat.value.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : combat.value.armor.regular.points);
  for (size_t i = 0; i < k->entry_count; i++) {
    const entry *e = &k->entries[i];
    inventory_set(state, e->info.weapon_inventory_index, !has_actor ? 0 : (double)(count(a, actor, e->definition->item) > 0));
    inventory_set(state, e->info.ammo_inventory_index, !has_actor || e->definition->ammo == NULL ? 0 : count(a, actor, e->definition->ammo));
  }
}
static qa_bot_weapon_knowledge_bots_a *candidates(void *ctx, qa_bot_library_bots_a *library, int64_t handle, size_t *out_count) {
  (void)library; /* TS (_library, handle) */
  qa_app_sim_bot_arsenal *a = ctx;
  q2_knowledge *k = a->impl;
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_handle_actor(a, handle, &actor);
  qa_bot_weapon_knowledge_bots_a *out = calloc(k->entry_count != 0 ? k->entry_count : 1, sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < k->entry_count; i++) {
    const entry *e = &k->entries[i];
    qa_bot_weapon_knowledge_bots_a *c = &out[i];
    c->info = e->info;
    c->has_maximum_range = true; c->maximum_range = e->shot.range;
    c->melee = false; c->has_personality_role = false;
    c->has_supply = true;
    c->supply.weapon = e->definition->item;
    c->supply.owned = has_actor && count(a, actor, e->definition->item) > 0;
    c->supply.has_ammo = e->definition->ammo != NULL; c->supply.ammo_item = e->definition->ammo;
    c->supply.per_shot = e->definition->quantity;
  }
  *out_count = k->entry_count;
  return out;
}
static const entry *entry_for(const q2_knowledge *k, int64_t slot) {
  for (size_t i = 0; i < k->entry_count; i++) if (k->entries[i].slot == slot) return &k->entries[i];
  return NULL;
}
static qa_item_id resolve_weapon(qa_app_sim_bot_arsenal *a, int64_t client, int64_t decision_slot) {
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_actor(a, client, &actor);
  const entry *e = entry_for(a->impl, decision_slot);
  return has_actor && e != NULL && can_use(a, actor, e) ? e->definition->item : NULL;
}
static const qa_q2w_state *weapon_state(qa_app_sim_bot_arsenal *a, int64_t client) {
  q2_knowledge *k = a->impl;
  qa_actor_id actor;
  if (!qa_app_sim_bot_arsenal_actor(a, client, &actor)) return NULL;
  return qa_q2w_ballistics_state(&k->weapons->base, actor);
}
static int64_t source_weapon_state(qa_app_sim_bot_arsenal *a, int64_t client) {
  const qa_q2w_state *state = weapon_state(a, client);
  if (state == NULL) return QA_Q3_WEAPON_READY;
  return state->phase == QA_Q2W_ACTIVATING ? QA_Q3_WEAPON_RAISING : state->phase == QA_Q2W_DROPPING ? QA_Q3_WEAPON_DROPPING
      : state->phase == QA_Q2W_FIRING ? QA_Q3_WEAPON_FIRING : QA_Q3_WEAPON_READY;
}
static int64_t source_weapon(qa_app_sim_bot_arsenal *a, int64_t client) {
  q2_knowledge *k = a->impl;
  const qa_q2w_state *state = weapon_state(a, client);
  const char *current = state == NULL ? NULL : state->weapon;
  for (size_t i = 0; i < k->entry_count; i++) if (current != NULL && is(k->entries[i].definition->name, current)) return k->entries[i].slot;
  return 0;
}
static void destroy_impl(void *impl) { q2_knowledge *k = impl; free(k->entries); free(k); }

qa_app_sim_bot_arsenal *qa_app_sim_create_q2_bot_knowledge(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx) {
  q2_knowledge *k = calloc(1, sizeof *k);
  if (k == NULL) qa_range_error("Out of memory");
  if (!simulation->q2_weapon_source(simulation->ctx, &k->game, &k->weapons)) {
    free(k); qa_raise("Error", 0, 0, "Q2 bot weapon knowledge requires a selected Q2 arsenal");
  }
  size_t definition_count = 0;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(k->weapons, &definition_count);
  qa_app_sim_bot_arsenal *a = qa_app_sim_bot_arsenal_alloc("q2", simulation, actor_for_client, actor_ctx);
  k->entries = calloc(definition_count != 0 ? definition_count : 1, sizeof *k->entries);
  a->uncovered_weapons = calloc(definition_count != 0 ? definition_count : 1, sizeof *a->uncovered_weapons);
  if (k->entries == NULL || a->uncovered_weapons == NULL) qa_range_error("Out of memory");
  bool rerelease = k->game->options.edition == QA_Q2_EDITION_RERELEASE, deathmatch = k->game->options.mode == QA_Q2_MODE_DEATHMATCH;
  for (size_t index = 0; index < definition_count; index++) {
    const qa_q2w_definition *definition = definitions[index];
    ballistics s;
    if (!weapon_ballistics(definition->name, rerelease, deathmatch, &s) || s.gravity > 0) {
      a->uncovered_weapons[a->uncovered_count++] = definition->name; continue;
    }
    entry *e = &k->entries[k->entry_count++];
    e->slot = (int64_t)index + 1; e->definition = definition; e->shot = s;
    e->info = weapon_info(definition, e->slot, s, rerelease);
  }
  a->impl = k; a->destroy_impl = destroy_impl;
  a->resolve_weapon = resolve_weapon; a->source_weapon = source_weapon; a->source_weapon_state = source_weapon_state;
  qa_app_sim_bot_arsenal_bind(a, update_inventory, candidates);
  return a;
}
