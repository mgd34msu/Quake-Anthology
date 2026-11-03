/* Port of src/app/bootstrap/simulation/bot-q3-knowledge.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_q3_knowledge.h"
#include <stdlib.h>
#include <string.h>
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

typedef struct q3_knowledge {
  qa_app_q3_selected_arsenal *source;
  const qa_q3_weapon_item *entries[QA_Q3_WEAPON_ITEM_COUNT]; size_t entry_count;
} q3_knowledge;

static void inventory_set(qa_bot_state_bots_a *state, size_t index, double value) {
  if (index < QA_Q3_BASE_MAX_ITEMS) state->src->inventory[index] = qa_js_to_int32(value); /* Int32Array store */
}
/* TS actorForClient: options.actorForClient(client) filtered by source.has. */
static bool actor_for_client(qa_app_sim_bot_arsenal *a, int64_t client, qa_actor_id *out) {
  q3_knowledge *k = a->impl;
  qa_actor_id actor;
  if (!qa_app_sim_bot_arsenal_actor(a, client, &actor) || !qa_app_q3_selected_arsenal_has(k->source, actor)) return false;
  *out = actor; return true;
}
/* TS sourceState: false is TS null. */
static bool source_state(qa_app_sim_bot_arsenal *a, int64_t client, qa_weapon_state *out) {
  q3_knowledge *k = a->impl;
  qa_actor_id actor;
  if (!actor_for_client(a, client, &actor)) return false;
  qa_arsenal_state arsenal = qa_app_q3_selected_arsenal_read(k->source, actor);
  if (!(arsenal.state.kind != NULL && strcmp(arsenal.state.kind, "q3") == 0)) qa_raise("Error", 0, 0, "Selected Q3 source returned a foreign arsenal");
  *out = arsenal.state; return true;
}
typedef struct owned_ctx { qa_app_sim_bot_arsenal *arsenal; bool has_actor; qa_actor_id actor; } owned_ctx;
static bool weapon_owned(void *ctx, int64_t weapon) {
  owned_ctx *o = ctx;
  const qa_q3_weapon_item *entry = qa_q3_get_weapon_item((double)weapon);
  return o->has_actor && entry != NULL && qa_shared_inventory_table_count(o->arsenal->simulation->inventory, o->actor, entry->item) > 0;
}
static double weapon_ammo(void *ctx, int64_t weapon) {
  owned_ctx *o = ctx;
  const qa_q3_weapon_item *entry = qa_q3_get_weapon_item((double)weapon);
  return !o->has_actor || entry == NULL || entry->ammo == NULL ? 0 : qa_shared_inventory_table_count(o->arsenal->simulation->inventory, o->actor, entry->ammo);
}
static void update_inventory(void *ctx, qa_bot_state_bots_a *state) {
  qa_app_sim_bot_arsenal *a = ctx;
  owned_ctx o;
  memset(&o, 0, sizeof o);
  o.arsenal = a;
  o.has_actor = actor_for_client(a, state->src->client, &o.actor);
  qa_app_sim_bot_arsenal_track(a, state->src->ws, o.has_actor ? &o.actor : NULL);
  for (size_t index = 0; index < 200; index++) state->src->inventory[index] = 0;
  qa_combat_state_result combat = {0};
  if (o.has_actor) combat = qa_gameplay_authority_read(a->simulation->combat, o.actor);
  inventory_set(state, QA_INV_HEALTH, combat.has_value ? combat.value.health : 0);
  inventory_set(state, QA_INV_ARMOR, !combat.has_value || combat.value.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : combat.value.armor.regular.points);
  qa_update_q3_bot_weapon_inventory_bots_a(state, weapon_owned, weapon_ammo, &o);
}
static qa_bot_weapon_knowledge_bots_a *candidates(void *ctx, qa_bot_library_bots_a *library, int64_t handle, size_t *out_count) {
  qa_app_sim_bot_arsenal *a = ctx;
  q3_knowledge *k = a->impl;
  qa_actor_id actor;
  bool has_actor = qa_app_sim_bot_arsenal_handle_actor(a, handle, &actor);
  qa_bot_weapon_knowledge_bots_a *out = calloc(k->entry_count != 0 ? k->entry_count : 1, sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  size_t n = 0;
  for (size_t i = 0; i < k->entry_count; i++) {
    const qa_q3_weapon_item *entry = k->entries[i];
    qa_weapon_info_bots_a info;
    if (!qa_weapon_ai_get_weapon_info(&library->weapons, handle, (int64_t)entry->weapon, &info) || !info.valid) continue;
    qa_bot_weapon_knowledge_bots_a *c = &out[n++];
    c->info = info;
    c->has_maximum_range = entry->weapon == QA_Q3_WP_GAUNTLET; c->maximum_range = 60;
    c->melee = entry->weapon == QA_Q3_WP_GAUNTLET;
    c->has_personality_role = true; c->personality_role = (int64_t)entry->weapon;
    c->has_supply = true;
    c->supply.weapon = entry->item;
    c->supply.owned = has_actor && qa_shared_inventory_table_count(a->simulation->inventory, actor, entry->item) > 0;
    c->supply.has_ammo = entry->ammo != NULL; c->supply.ammo_item = entry->ammo; c->supply.per_shot = 1;
  }
  *out_count = n;
  return out;
}
static qa_item_id resolve_weapon(qa_app_sim_bot_arsenal *a, int64_t client, int64_t decision_slot) {
  q3_knowledge *k = a->impl;
  qa_actor_id actor;
  bool has_actor = actor_for_client(a, client, &actor);
  const qa_q3_weapon_item *entry = NULL;
  for (size_t i = 0; i < k->entry_count && entry == NULL; i++) if (k->entries[i]->weapon == (double)decision_slot) entry = k->entries[i];
  qa_shared_inventory_table *inventory = a->simulation->inventory;
  return has_actor && entry != NULL && qa_shared_inventory_table_count(inventory, actor, entry->item) > 0
      && (entry->ammo == NULL || qa_shared_inventory_table_count(inventory, actor, entry->ammo) != 0) ? entry->item : NULL;
}
static int64_t source_weapon(qa_app_sim_bot_arsenal *a, int64_t client) {
  qa_weapon_state state;
  return source_state(a, client, &state) ? (int64_t)state.source_weapon : QA_Q3_WP_NONE;
}
static int64_t source_weapon_state(qa_app_sim_bot_arsenal *a, int64_t client) {
  qa_weapon_state state;
  double phase = source_state(a, client, &state) ? state.state : QA_Q3_WEAPON_READY;
  if (phase == QA_Q3_WEAPON_READY) return QA_Q3_WEAPON_READY;
  if (phase == QA_Q3_WEAPON_RAISING) return QA_Q3_WEAPON_RAISING;
  if (phase == QA_Q3_WEAPON_DROPPING) return QA_Q3_WEAPON_DROPPING;
  if (phase == QA_Q3_WEAPON_FIRING) return QA_Q3_WEAPON_FIRING;
  qa_raise("Error", 0, 0, "Selected Q3 source has an invalid weapon phase");
}

qa_app_sim_bot_arsenal *qa_app_sim_create_q3_bot_knowledge(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client_fn, void *actor_ctx) {
  qa_app_q3_selected_arsenal *source = simulation->selected_q3_weapon_source(simulation->ctx);
  if (source == NULL) qa_raise("Error", 0, 0, "Q3 bot weapon knowledge requires an actual selected Q3 arsenal");
  q3_knowledge *k = calloc(1, sizeof *k);
  if (k == NULL) qa_range_error("Out of memory");
  k->source = source;
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++)
    if (qa_q3_weapon_items[i].weapon <= QA_Q3_WP_GRAPPLING_HOOK) k->entries[k->entry_count++] = &qa_q3_weapon_items[i];
  qa_app_sim_bot_arsenal *a = qa_app_sim_bot_arsenal_alloc("q3", simulation, actor_for_client_fn, actor_ctx);
  a->impl = k; a->destroy_impl = free;
  a->resolve_weapon = resolve_weapon; a->source_weapon = source_weapon; a->source_weapon_state = source_weapon_state;
  qa_app_sim_bot_arsenal_bind(a, update_inventory, candidates);
  return a;
}
