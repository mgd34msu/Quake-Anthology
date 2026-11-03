/* SharedSimulation checkpoint and restore (runtime.ts): checkpoint,
 * checkpointLoading, checkpointFromSource, restoreNativeLoading, restore,
 * and the B1 rows that need the Q1 foundation checkpoint in both C shapes
 * (captureSelectedMonsters, restoreSelectedMonsters, captureForeignQ1World,
 * restoreForeignQ1World). The world:simulation record is built as a
 * checkpoint-value tree in the TS object-literal key order, since TS hands
 * that plain object to encodeCheckpointValue. GPL-2.0-or-later. */
#include "shared_simulation_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/persistence/protection.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_missionpacks.h"
#include "qa/persistence/q2_monsters.h"
#include "qa/persistence/q2_players.h"
#include "qa/persistence/q2_weapons.h"
#include "qa/content/composition/q2/save.h"
#include "qa/app/bootstrap/simulation/player_checkpoint.h"
#include "qa/app/bootstrap/simulation/mod_client_checkpoint.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/source_items.h"
#include "qa/persistence/shared.h"
#include "qa/app/bootstrap/simulation/weapon_slot_checkpoint.h"
#include "qa/app/bootstrap/simulation/grapple_checkpoint.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/recipe.h"
#include "qa/app/bootstrap/simulation/equipment_checkpoint.h"
#include "qa/settings/server/q2_owner.h"
#include "qa/content/composition/expansion_supply.h"
#include "qa/app/bootstrap/simulation/native_q2_map.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/actors/body.h"
#include "qa/persistence/world_state.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/compat/q2/rerelease/deferred_damage.h"
#include "qa/content/q3/base/combat_bridge.h"
/* TS leaves an unregistered Q3 bridge policy to the GC; the simulation frees it with itself. */
static void ss_release_q3_bridge_policy(void *ctx) {
  qa_combat_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.ctx = ctx;
  qa_q3_base_combat_bridge_policy_destroy(policy);
}

static noreturn void ck_fail(const char *format, ...) {
  char text[512]; va_list args; va_start(args, format); vsnprintf(text, sizeof text, format, args); va_end(args);
  qa_range_error(text);
}
static void ck_oom(const void *ptr) { if (ptr == NULL) qa_range_error("Out of memory"); }

/* ---- checkpoint-value tree builders (TS plain records; OOM aborts) ---- */
static qa_save_value *ck_obj(void) { qa_save_value *v = qa_save_value_object(); ck_oom(v); return v; }
static qa_save_value *ck_arr(void) { qa_save_value *v = qa_save_value_array(); ck_oom(v); return v; }
static void ck_set(qa_save_value *o, const char *key, qa_save_value *child) {
  ck_oom(child);
  if (!qa_save_value_set(o, key, child)) { qa_save_value_free(child); ck_oom(NULL); }
}
static void ck_push(qa_save_value *a, qa_save_value *child) {
  ck_oom(child);
  if (!qa_save_value_push(a, child)) { qa_save_value_free(child); ck_oom(NULL); }
}
static qa_save_value *ck_num(double value) { return qa_save_value_number(value); }
static qa_save_value *ck_str(const char *value) { return qa_save_value_string(value); }
static qa_save_value *ck_bool(bool value) { return qa_save_value_boolean(value); }
static qa_save_value *ck_null(void) { return qa_save_value_null(); }
static qa_save_value *ck_actor(qa_actor_id actor) { return qa_save_actor_value(qa_saved_actor_id_of(actor)); }
static qa_save_value *ck_saved(qa_saved_actor_id actor) { return qa_save_actor_value(actor); }
/* A tree from encoder bytes (decode is the inverse of encodeCheckpointValue). */
/* length is read through a pointer: callers pass the encoder call that
 * sets it as the other argument, and C leaves argument order unspecified. */
static qa_save_value *ck_tree(uint8_t *bytes, const size_t *length) {
  ck_oom(bytes);
  qa_save_value *value = qa_decode_checkpoint_value(bytes, *length);
  free(bytes);
  ck_oom(value);
  return value;
}
static qa_save_value *ck_vector(qa_vec3 v) { return qa_save_vector_value(v); }

/* ---- the Q1 foundation checkpoint in both C shapes ---- */
/* TS game.capture() as an owned reader-shape checkpoint (root owned). */
static void ck_q1_capture(qa_q1_services *game, qa_persist_q1_foundation_checkpoint *out) {
  qa_q1_foundation_checkpoint content;
  if (!qa_q1_services_capture(game, &content)) qa_range_error("Out of memory");
  qa_persist_q1_foundation_checkpoint view;
  qa_persist_q1_foundation_from_content(&content, &view);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q1_foundation_checkpoint(&view, &length);
  qa_q1_foundation_checkpoint_free(&view);
  qa_q1_checkpoint_free(&content);
  ck_oom(bytes);
  bool ok = qa_decode_q1_foundation_checkpoint(bytes, length, out);
  free(bytes);
  if (!ok) qa_range_error("Out of memory");
}
/* TS game.capture() as the plain record encodeCheckpointValue sees. */
static qa_save_value *ck_q1_capture_value(qa_q1_services *game) {
  qa_q1_foundation_checkpoint content;
  if (!qa_q1_services_capture(game, &content)) qa_range_error("Out of memory");
  qa_persist_q1_foundation_checkpoint view;
  qa_persist_q1_foundation_from_content(&content, &view);
  qa_save_value *value = qa_q1_foundation_checkpoint_value(&view);
  qa_q1_foundation_checkpoint_free(&view);
  qa_q1_checkpoint_free(&content);
  ck_oom(value);
  return value;
}
/* TS game.restore(checkpoint, { scheduleThinks }). */
static void ck_q1_restore(qa_q1_services *game, const qa_persist_q1_foundation_checkpoint *saved, bool schedule_thinks) {
  qa_q1_foundation_checkpoint content;
  qa_persist_q1_foundation_to_content(saved, &content);
  qa_q1_services_restore(game, &content, schedule_thinks);
  qa_persist_q1_foundation_content_free(&content);
}

/* TS SourceRandom.checkpoint() as its plain record. */
static qa_save_value *ck_random_value(qa_app_sim_source_random *random) {
  qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(random);
  double words[QA_Q2_RERELEASE_WORDS];
  qa_random_state out; memset(&out, 0, sizeof out);
  if (qa_ss_is(state.kind, "q2-rerelease-mt19937")) {
    out.kind = QA_RANDOM_STATE_Q2_RERELEASE_MT19937;
    for (size_t i = 0; i < QA_Q2_RERELEASE_WORDS; i++) words[i] = state.rerelease.words[i];
    out.words = (qa_double_view){words, QA_Q2_RERELEASE_WORDS};
    out.index = (double)state.rerelease.index; out.draws = (double)state.rerelease.draws;
  } else {
    out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
    for (size_t i = 0; i < 31; i++) words[i] = state.words[i];
    out.words = (qa_double_view){words, 31};
    out.front = state.front; out.rear = state.rear; out.draws = (double)state.draws;
  }
  return qa_save_random_value(out);
}
/* TS SourceRandom.restore(state) from a read RandomState. */
static void ck_random_restore(qa_app_sim_source_random *random, const qa_random_state *state) {
  qa_app_sim_source_random_restore(random, qa_app_sim_source_random_state_of(state));
}

/* ---- persistence host views of the shared owners ----
 * persistence/source_items.h, protection.h and world_state.h take the
 * registry, body table, authority and inventory as fn+ctx tables (TS
 * passes the owners themselves). In this translation unit those struct
 * tags carry the table layout, so sim->actors, the body table,
 * sim->combat and sim->inventory are the opaque owners and travel only
 * as ctx. */
typedef struct ck_hosts {
  qa_session_actor_registry actors;
  qa_shared_body_table bodies;
  qa_gameplay_authority combat;
  qa_shared_inventory_table inventory;
  void *real_actors, *real_bodies, *real_combat, *real_inventory;
  qa_source_item_group *groups; size_t group_capacity;
} ck_hosts;
static const qa_actor_observation *ck_h_observations(void *ctx, size_t *out_count) {
  return qa_session_actor_registry_observations(((ck_hosts *)ctx)->real_actors, out_count);
}
static bool ck_h_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(((ck_hosts *)ctx)->real_actors, actor, out);
}
static bool ck_h_resolve_saved(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_saved(((ck_hosts *)ctx)->real_actors, saved, out);
}
static qa_actor_id ck_h_reference_saved(void *ctx, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(((ck_hosts *)ctx)->real_actors, saved, "checkpoint");
}
static bool ck_h_body_read(void *ctx, qa_actor_id actor, qa_body_state *out) {
  qa_body_state_result result = qa_shared_body_table_read(((ck_hosts *)ctx)->real_bodies, actor);
  if (result.has_value) *out = result.value;
  return result.has_value;
}
static bool ck_h_link_state(void *ctx, qa_actor_id actor, qa_persist_body_link_state *out) {
  qa_body_link_state state;
  if (!qa_shared_body_table_link_state(((ck_hosts *)ctx)->real_bodies, actor, &state)) return false;
  memset(out, 0, sizeof *out);
  out->link_count = state.link_count; out->has_linked = state.has_linked;
  if (state.has_linked) { out->state = state.linked.state; out->absolute_bounds = state.linked.absolute_bounds; }
  return true;
}
static bool ck_h_attachment(void *ctx, qa_actor_id actor, qa_body_attachment *out) {
  qa_body_attachment_result result = qa_shared_body_table_attachment(((ck_hosts *)ctx)->real_bodies, actor);
  if (result.has_value) *out = result.value;
  return result.has_value;
}
static void ck_h_restore_link_state(void *ctx, qa_owned_actor actor, qa_persist_body_link_state state) {
  qa_body_link_state saved; memset(&saved, 0, sizeof saved);
  saved.link_count = state.link_count; saved.has_linked = state.has_linked;
  if (state.has_linked) {
    saved.linked.actor = actor.id; saved.linked.state = state.state;
    saved.linked.absolute_bounds = state.absolute_bounds; saved.linked.link_count = state.link_count;
  }
  qa_shared_body_table_restore_link_state(((ck_hosts *)ctx)->real_bodies, actor, saved);
}
static void ck_h_body_create(void *ctx, qa_owned_actor actor, qa_body_state state) {
  qa_shared_body_table_create_body(((ck_hosts *)ctx)->real_bodies, actor, state);
}
static void ck_h_body_attach(void *ctx, qa_owned_actor actor, qa_body_attachment attachment) {
  qa_shared_body_table_attach(((ck_hosts *)ctx)->real_bodies, actor, attachment);
}
static void ck_h_assert_idle(void *ctx) { qa_gameplay_authority_assert_idle(((ck_hosts *)ctx)->real_combat); }
static bool ck_h_combat_read(void *ctx, qa_actor_id actor, qa_combat_state *out) {
  qa_combat_state_result result = qa_gameplay_authority_read(((ck_hosts *)ctx)->real_combat, actor);
  if (result.has_value) *out = result.value;
  return result.has_value;
}
static void ck_h_combat_create(void *ctx, qa_owned_actor actor, qa_combat_state state) {
  qa_gameplay_authority_create_state(((ck_hosts *)ctx)->real_combat, actor, state, NULL, NULL);
}
static qa_armor_state ck_h_normalize_legacy_armor(void *ctx, qa_owned_actor actor, qa_armor_state armor) {
  return qa_gameplay_authority_normalize_legacy_armor(((ck_hosts *)ctx)->real_combat, actor, armor);
}
static qa_provider_id ck_h_protection_owner(void *ctx, qa_owned_actor actor, qa_protection_channel channel) {
  qa_provider_id owner = NULL;
  return qa_gameplay_authority_protection_owner(((ck_hosts *)ctx)->real_combat, actor, channel, &owner) ? owner : NULL;
}
static const qa_item_id *ck_h_protection_items(void *ctx, qa_owned_actor actor, qa_protection_channel channel, size_t *out_count) {
  return qa_gameplay_authority_protection_inventory_items(((ck_hosts *)ctx)->real_combat, actor, channel, out_count);
}
static bool ck_h_copied_primary_armor(void *ctx, qa_owned_actor actor, qa_armor_state *out) {
  return qa_gameplay_authority_copied_primary_armor(((ck_hosts *)ctx)->real_combat, actor, out);
}
static void ck_h_set_powered(void *ctx, qa_owned_actor actor, qa_powered_protection_state powered) {
  qa_gameplay_authority_set_powered_protection(((ck_hosts *)ctx)->real_combat, actor, powered);
}
static bool ck_h_source_items(void *ctx, qa_owned_actor actor, qa_source_item_snapshot *out) {
  ck_hosts *hosts = ctx;
  qa_inventory_source_items_result result = qa_shared_inventory_table_source_items(hosts->real_inventory, actor);
  if (!result.has_value) return false;
  if (result.group_count > hosts->group_capacity) {
    qa_source_item_group *grown = realloc(hosts->groups, result.group_count * sizeof *grown);
    ck_oom(grown);
    hosts->groups = grown; hosts->group_capacity = result.group_count;
  }
  for (size_t i = 0; i < result.group_count; i++) {
    hosts->groups[i].owner = result.groups[i].owner;
    hosts->groups[i].items = result.groups[i].items;
    hosts->groups[i].item_count = result.groups[i].item_count;
  }
  out->primary = result.primary; out->primary_count = result.primary_count;
  out->groups = hosts->groups; out->group_count = result.group_count;
  return true;
}
static const qa_inventory_entry *ck_h_entries(void *ctx, qa_actor_id actor, size_t *out_count) {
  return qa_shared_inventory_table_entries(((ck_hosts *)ctx)->real_inventory, actor, out_count);
}
static bool ck_h_has(void *ctx, qa_actor_id actor) { return qa_shared_inventory_table_has(((ck_hosts *)ctx)->real_inventory, actor); }
static void ck_h_inventory_create(void *ctx, qa_owned_actor actor, const qa_inventory_entry *entries, size_t count) {
  qa_shared_inventory_table_create_entries(((ck_hosts *)ctx)->real_inventory, actor, entries, count);
}
static void ck_hosts_init(ck_hosts *hosts, qa_app_shared_simulation *sim) {
  memset(hosts, 0, sizeof *hosts);
  hosts->real_actors = sim->actors; hosts->real_bodies = qa_app_sim_shared_physics_bodies(sim->physics);
  hosts->real_combat = sim->combat; hosts->real_inventory = sim->inventory;
  hosts->actors = (qa_session_actor_registry){hosts, ck_h_observations, ck_h_resolve_owned, ck_h_resolve_saved, ck_h_reference_saved};
  hosts->bodies = (qa_shared_body_table){hosts, ck_h_body_read, ck_h_link_state, ck_h_attachment, ck_h_restore_link_state,
      ck_h_body_create, ck_h_body_attach};
  hosts->combat = (qa_gameplay_authority){hosts, ck_h_assert_idle, ck_h_combat_read, ck_h_combat_create, ck_h_normalize_legacy_armor,
      ck_h_protection_owner, ck_h_protection_items, ck_h_copied_primary_armor, ck_h_set_powered};
  hosts->inventory = (qa_shared_inventory_table){hosts, ck_h_source_items, ck_h_entries, ck_h_has, ck_h_inventory_create};
}
static void ck_hosts_free(ck_hosts *hosts) { free(hosts->groups); hosts->groups = NULL; hosts->group_capacity = 0; }

/* ---- B1: selected monsters and the foreign Q1 world ---- */
static qa_save_value *ck_definition_value(qa_monster_definition_reference definition) {
  qa_save_value *o = ck_obj();
  ck_set(o, "source", qa_save_provider_value(definition.source));
  ck_set(o, "classname", ck_str(definition.classname));
  return o;
}
static qa_save_value *ck_nullable_saved(bool has, qa_saved_actor_id actor) { return has ? ck_saved(actor) : ck_null(); }
/* SelectedMonsters.capture() entry: `{ ...entry, definition, ... }` keeps the
 * AuthoredMonster key order of admit() and appends definition. */
static qa_save_value *ck_authored_value(const qa_app_sim_saved_authored_monster *entry) {
  qa_save_value *o = ck_obj();
  ck_set(o, "actor", ck_saved(entry->actor));
  ck_set(o, "classname", ck_str(entry->classname));
  ck_set(o, "sourceOrdinal", ck_num(entry->source_ordinal));
  ck_set(o, "spawnflags", ck_num(entry->spawnflags));
  ck_set(o, "deathTarget", ck_str(entry->death_target));
  ck_set(o, "dropItem", ck_str(entry->drop_item));
  ck_set(o, "targetname", ck_str(entry->targetname));
  ck_set(o, "target", ck_str(entry->target));
  ck_set(o, "killtarget", ck_str(entry->killtarget));
  ck_set(o, "message", ck_str(entry->message));
  ck_set(o, "delay", ck_num(entry->delay));
  ck_set(o, "route", ck_str(entry->route));
  ck_set(o, "routeGoal", ck_nullable_saved(entry->has_route_goal, entry->route_goal));
  ck_set(o, "routeResolved", ck_bool(entry->route_resolved));
  ck_set(o, "countedDeath", ck_bool(entry->counted_death));
  ck_set(o, "combatTarget", ck_str(entry->combat_target));
  ck_set(o, "combatGoal", ck_nullable_saved(entry->has_combat_goal, entry->combat_goal));
  ck_set(o, "standGround", ck_bool(entry->stand_ground));
  qa_save_value *placement = ck_obj();
  ck_set(placement, "kind", ck_str(entry->placement.kind));
  if (qa_ss_is(entry->placement.kind, "teleport")) ck_set(placement, "origin", ck_vector(entry->placement.origin));
  else if (qa_ss_is(entry->placement.kind, "waiting")) {
    qa_save_value *barriers = ck_arr();
    for (size_t i = 0; i < entry->placement.barrier_count; i++) {
      qa_save_value *barrier = ck_obj();
      ck_set(barrier, "actor", ck_saved(entry->placement.barriers[i].actor));
      ck_set(barrier, "origin", ck_vector(entry->placement.barriers[i].origin));
      ck_push(barriers, barrier);
    }
    ck_set(placement, "barriers", barriers);
    ck_set(placement, "activator", ck_nullable_saved(entry->placement.has_activator, entry->placement.activator));
  }
  ck_set(o, "placement", placement);
  qa_save_value *activation = ck_obj();
  ck_set(activation, "kind", ck_str(entry->activation.kind));
  if (qa_ss_is(entry->activation.kind, "scheduled")) {
    ck_set(activation, "at", ck_num(entry->activation.at));
    ck_set(activation, "activator", ck_nullable_saved(entry->activation.has_activator, entry->activation.activator));
  }
  ck_set(o, "activation", activation);
  ck_set(o, "definition", ck_definition_value(entry->definition));
  return o;
}
/* Pick<Q2WeaponsCheckpoint, "blasterCauses">. */
static qa_save_value *ck_ballistics_value(qa_q2w_ballistics *ballistics) {
  qa_q2w_cause_entry *causes = NULL; size_t count = 0;
  if (!qa_q2w_ballistics_capture_projectiles(ballistics, &causes, &count)) qa_range_error("Out of memory");
  qa_save_value *o = ck_obj(), *list = ck_arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_saved(causes[i].actor));
    ck_set(entry, "meansOfDeath", ck_num(causes[i].means_of_death));
    ck_push(list, entry);
  }
  qa_q2w_free_cause_entries(causes);
  ck_set(o, "blasterCauses", list);
  return o;
}
static qa_save_value *ck_q2_entities_value(qa_q2_entity_services *game) {
  qa_q2_foundation_checkpoint entities = qa_q2_entity_services_capture(game);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_foundation_checkpoint(&entities, &length);
  return ck_tree(bytes, &length);
}
static qa_save_value *ck_q2_monsters_value(qa_q2m_monsters *monsters) {
  qa_q2m_monsters_checkpoint checkpoint;
  if (!qa_q2m_monsters_capture(monsters, &checkpoint)) qa_range_error("Out of memory");
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_monsters_checkpoint(&checkpoint, &length);
  qa_q2m_monsters_checkpoint_free(&checkpoint);
  return ck_tree(bytes, &length);
}
static qa_save_value *ck_q2_movers_value(qa_q2_mover_module *movers, qa_q2_game_services *game) {
  qa_q2_movers_checkpoint checkpoint = qa_q2_mover_module_capture(movers, game);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_movers_checkpoint(&checkpoint, &length);
  return ck_tree(bytes, &length);
}
static qa_save_value *ck_q2_pack_monsters_value(qa_q2mpm_monsters_module *monsters, qa_q2_game_services *game) {
  qa_q2mpm_monsters_checkpoint checkpoint;
  if (!qa_q2mpm_monsters_capture(monsters, game, &checkpoint)) qa_range_error("Out of memory");
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_mission_pack_monsters_checkpoint(&checkpoint, &length);
  qa_q2mpm_monsters_checkpoint_free(&checkpoint);
  return ck_tree(bytes, &length);
}

/* captureSelectedMonsters(): the SelectedMonstersCheckpoint record, or a
 * null value (TS null) without a selected admission. */
qa_save_value *qa_app_shared_sim_capture_selected_monsters(qa_app_shared_simulation *sim) {
  if (sim->selected_monsters == NULL) return ck_null();
  qa_save_value *o = ck_obj();
  ck_set(o, "version", ck_num(2));
  qa_app_sim_saved_authored_monsters authored = qa_app_sim_selected_monsters_capture(sim->selected_monsters);
  qa_save_value *list = ck_arr();
  for (size_t i = 0; i < authored.count; i++) ck_push(list, ck_authored_value(&authored.entries[i]));
  qa_app_sim_saved_authored_monsters_free(&authored);
  ck_set(o, "authored", list);
  qa_save_value *sources = ck_arr();
  for (size_t i = 0; i < sim->monster_sources.count; i++) {
    qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    qa_save_value *entry = ck_obj();
    ck_set(entry, "reference", qa_save_provider_value(source->reference));
    ck_set(entry, "frame", qa_save_frame_value(source->clock.frame));
    ck_set(entry, "random", ck_random_value(source->random));
    ck_set(entry, "kind", ck_str(source->kind));
    if (qa_ss_is(source->kind, "q1")) ck_set(entry, "entities", ck_q1_capture_value(source->game));
    else {
      qa_app_sim_selected_q2_monster_modules *q2 = source->q2;
      qa_q2_game_services *game = qa_q2_entity_services_game(q2->game);
      ck_set(entry, "entities", ck_q2_entities_value(q2->game));
      ck_set(entry, "monsters", ck_q2_monsters_value(&q2->monsters));
      ck_set(entry, "ballistics", ck_ballistics_value(&q2->ballistics));
      ck_set(entry, "movers", ck_q2_movers_value(q2->movers, game));
      qa_save_value *packs = ck_arr();
      for (size_t p = 0; p < q2->pack_count; p++) {
        qa_save_value *pack = ck_obj();
        ck_set(pack, "pack", ck_str(q2->packs[p].pack));
        ck_set(pack, "state", ck_q2_pack_monsters_value(&q2->packs[p].monsters, game));
        ck_push(packs, pack);
      }
      ck_set(entry, "packs", packs);
    }
    ck_push(sources, entry);
  }
  ck_set(o, "sources", sources);
  return o;
}

void qa_app_shared_sim_restore_selected_monsters(qa_app_shared_simulation *sim, const qa_app_sim_selected_monsters_checkpoint *saved) {
  qa_app_sim_selected_monsters *selected = sim->selected_monsters;
  if (selected == NULL) ck_fail("Saved monsters have no selected admission");
  qa_app_sim_selected_monsters_restore(selected, saved->authored, saved->authored_count);
  size_t count = qa_app_sim_selected_monsters_count(selected);
  for (size_t i = 0; i < count; i++) {
    qa_authored_monster *entry = qa_app_sim_selected_monsters_at(selected, i);
    qa_actor_id id = entry->base.actor.id;
    const qa_monster_mission *mission = qa_app_sim_selected_monsters_mission(selected, id);
    if (mission != NULL) qa_ss_map_set(&sim->monster_missions, &id, mission);
    qa_monster_definition_reference definition;
    if (!qa_app_sim_selected_monsters_definition(selected, id, &definition)) ck_fail("Restored monster has no definition");
    qa_app_sim_selected_monster_source *source = qa_app_shared_sim_monster_source_for(sim, definition);
    if (qa_ss_is(source->kind, "q1") && mission != NULL) qa_q1_set_mission(source->game, id, mission);
  }
  if (sim->strategy->admits_guest_monsters(sim)) {
    qa_app_source_monster_proxy proxy;
    bool has_proxy = sim->strategy->monster_proxy(sim, &proxy);
    sim->guest_monsters_admitted = true;
    for (size_t i = 0; i < count; i++) {
      qa_authored_monster *entry = qa_app_sim_selected_monsters_at(selected, i);
      if (!qa_session_actor_registry_is_live(sim->actors, entry->base.actor.id)) continue;
      if (has_proxy) {
        double proxy_slot;
        if (!proxy.allocate(sim, &proxy_slot)) ck_fail("Selected monster projection lost its declared combat profile");
        qa_session_actor_registry_bind_source(sim->actors, entry->base.actor, sim->recipe.map.entities.provider, proxy_slot);
        proxy.bind(sim, proxy_slot, entry->base.actor);
      }
    }
  }
  for (size_t i = 0; i < saved->source_count; i++) {
    const qa_app_sim_monster_source_checkpoint *state = &saved->sources[i];
    qa_app_sim_selected_monster_source **found = qa_ss_map_get(&sim->monster_sources, &state->reference.provider);
    qa_app_sim_selected_monster_source *source = found == NULL ? NULL : *found;
    if (source == NULL || !qa_ss_is(source->reference.content, state->reference.content) || !qa_ss_is(source->kind, state->kind))
      ck_fail("Saved monster source differs from selected module");
    source->clock.frame = state->frame; source->clock.advanced = false;
    ck_random_restore(source->random, &state->random);
    if (qa_ss_is(source->kind, "q1")) ck_q1_restore(source->game, &state->q1_entities, false);
    else {
      qa_app_sim_selected_q2_monster_modules *q2 = source->q2;
      qa_q2_game_services *game = qa_q2_entity_services_game(q2->game);
      qa_q2_entity_services_restore(q2->game, state->q2_entities);
      qa_q2m_monsters_restore(&q2->monsters, game, &state->monsters);
      qa_q2w_ballistics_restore_projectiles(&q2->ballistics, game, state->ballistics.blaster_causes, state->ballistics.blaster_cause_count);
      if (state->has_movers) qa_q2_mover_module_restore(q2->movers, game, state->movers);
      for (size_t p = 0; p < state->pack_count; p++) {
        qa_app_sim_q2_monster_pack *pack = NULL;
        for (size_t k = 0; k < q2->pack_count && pack == NULL; k++) if (qa_ss_is(q2->packs[k].pack, state->packs[p].pack)) pack = &q2->packs[k];
        if (pack == NULL) ck_fail("Saved monster pack is not registered");
        qa_q2mpm_monsters_restore(&pack->monsters, game, &state->packs[p].state);
      }
    }
  }
}

/* captureForeignQ1World(): the ForeignQ1WorldCheckpoint record or null. */
qa_save_value *qa_app_shared_sim_capture_foreign_q1_world(qa_app_shared_simulation *sim) {
  if (!sim->has_foreign_q1_world) return ck_null();
  qa_ss_foreign_q1_world *foreign = &sim->foreign_q1_world;
  qa_save_value *o = ck_obj();
  ck_set(o, "version", ck_num(1));
  ck_set(o, "reference", qa_save_provider_value(foreign->reference));
  ck_set(o, "edition", ck_str(foreign->edition));
  ck_set(o, "program", ck_str(foreign->program));
  ck_set(o, "frame", qa_save_frame_value(foreign->clock.frame));
  ck_set(o, "random", ck_random_value(foreign->random));
  ck_set(o, "entities", ck_q1_capture_value(foreign->modules->game));
  qa_save_value *adjunct = ck_obj();
  ck_set(adjunct, "flags", ck_num(foreign->campaign.flags));
  ck_set(adjunct, "skill", ck_num(foreign->campaign.skill));
  qa_save_value *cvars = ck_arr();
  for (size_t i = 0; i < foreign->cvar_name_count; i++) {
    qa_save_value *entry = ck_obj();
    ck_set(entry, "name", ck_str(foreign->cvar_names[i]));
    char *text = qa_cvar_registry_variable_string(foreign->cvars, foreign->cvar_names[i]);
    ck_set(entry, "value", ck_str(text == NULL ? "" : text));
    free(text);
    ck_push(cvars, entry);
  }
  ck_set(adjunct, "cvars", cvars);
  ck_set(o, "adjunct", adjunct);
  return o;
}

void qa_app_shared_sim_restore_foreign_q1_world(qa_app_shared_simulation *sim, const qa_app_sim_foreign_q1_world_checkpoint *saved) {
  if (!sim->has_foreign_q1_world) ck_fail("Saved foreign Q1 world has no adjunct");
  qa_ss_foreign_q1_world *foreign = &sim->foreign_q1_world;
  if (!qa_ss_is(foreign->reference.provider, saved->reference.provider) || !qa_ss_is(foreign->reference.content, saved->reference.content)
      || !qa_ss_is(foreign->edition, saved->edition) || !qa_ss_is(foreign->program, saved->program))
    ck_fail("Saved foreign Q1 world differs from its adjunct");
  foreign->clock.frame = saved->frame; foreign->clock.advanced = false;
  ck_random_restore(foreign->random, &saved->random);
  ck_q1_restore(foreign->modules->game, &saved->entities, false);
  if (saved->has_adjunct) {
    foreign->campaign.flags = saved->adjunct.flags; foreign->campaign.skill = (int)saved->adjunct.skill;
    for (size_t i = 0; i < saved->adjunct.cvar_count; i++) {
      bool listed = false;
      for (size_t k = 0; k < foreign->cvar_name_count && !listed; k++) listed = qa_ss_is(foreign->cvar_names[k], saved->adjunct.cvars[i].name);
      if (!listed) continue;
      qa_cvar_read snapshot;
      if (qa_cvar_registry_set(foreign->cvars, saved->adjunct.cvars[i].name, saved->adjunct.cvars[i].value, true, &snapshot)) qa_cvar_read_free(&snapshot);
    }
  }
}

/* ---- world:simulation record members ---- */
static qa_save_value *ck_nullable_actor(const qa_actor_id *actor) { return actor == NULL ? ck_null() : ck_actor(*actor); }
/* `{ ...collision, owner: saved }`: ActorCollision declaration order. */
static qa_save_value *ck_collision_value(const qa_actor_collision *collision) {
  qa_save_value *o = ck_obj();
  ck_set(o, "family", ck_str(collision->family));
  qa_save_value *shape = ck_obj();
  ck_set(shape, "kind", ck_str(collision->shape_kind));
  if (qa_ss_is(collision->shape_kind, "model")) ck_set(shape, "model", ck_num(collision->shape_model));
  ck_set(o, "shape", shape);
  ck_set(o, "contents", ck_num(collision->contents));
  ck_set(o, "owner", ck_nullable_actor(collision->owner));
  if (collision->has_q3_owner) {
    qa_save_value *q3 = ck_obj();
    ck_set(q3, "entityNumber", ck_num(collision->q3_entity_number));
    ck_set(q3, "ownerNumber", ck_num(collision->q3_owner_number));
    ck_set(o, "q3Owner", q3);
  }
  ck_set(o, "role", ck_str(collision->role));
  ck_set(o, "monster", ck_bool(collision->monster));
  ck_set(o, "deadMonster", ck_bool(collision->dead_monster));
  if (collision->q1_corpse) ck_set(o, "q1Corpse", ck_bool(true));
  return o;
}
static qa_save_value *ck_collision_entries(const qa_app_sim_physics_collision_entry *entries, size_t count) {
  qa_save_value *list = ck_arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_saved(entries[i].actor));
    ck_set(entry, "collision", ck_collision_value(&entries[i].collision));
    ck_push(list, entry);
  }
  return list;
}
/* SharedPhysics.capture(). Optional solid and flag members follow the
 * SharedSolid / SharedPhysicsFlags declaration order. */
static qa_save_value *ck_physics_value(qa_app_sim_shared_physics *physics) {
  qa_app_sim_physics_capture capture = qa_app_sim_shared_physics_capture(physics);
  qa_save_value *o = ck_obj();
  ck_set(o, "gravity", ck_num(capture.gravity));
  ck_set(o, "rereleaseMovement", ck_vector(capture.rerelease_movement));
  ck_set(o, "collisions", ck_collision_entries(capture.collisions, capture.collision_count));
  ck_set(o, "spatial", ck_collision_entries(capture.spatial, capture.spatial_count));
  qa_save_value *solids = ck_arr();
  for (size_t i = 0; i < capture.solid_count; i++) {
    const qa_app_sim_physics_solid_entry *entry = &capture.solids[i];
    const qa_app_sim_shared_solid *solid = &entry->solid;
    qa_save_value *value = ck_obj();
    ck_set(value, "actor", ck_saved(entry->actor));
    ck_set(value, "solid", ck_str(solid->solid));
    ck_set(value, "model", solid->has_model ? ck_num(solid->model) : ck_null());
    ck_set(value, "family", ck_str(solid->family));
    ck_set(value, "owner", ck_nullable_saved(entry->has_owner, entry->owner));
    if (solid->has_monster) ck_set(value, "monster", ck_bool(solid->monster));
    if (solid->has_dead_monster) ck_set(value, "deadMonster", ck_bool(solid->dead_monster));
    if (solid->q1_corpse) ck_set(value, "q1Corpse", ck_bool(true));
    if (solid->has_item) ck_set(value, "item", ck_bool(solid->item));
    ck_push(solids, value);
  }
  ck_set(o, "solids", solids);
  qa_save_value *motions = ck_arr();
  for (size_t i = 0; i < capture.motion_count; i++) {
    const qa_app_sim_physics_motion_entry *entry = &capture.motions[i];
    qa_save_value *value = ck_obj();
    ck_set(value, "actor", ck_saved(entry->actor));
    if (entry->motion.kind_first) ck_set(value, "kind", ck_str(entry->motion.kind));
    ck_set(value, "velocity", ck_vector(entry->motion.velocity));
    ck_set(value, "angularVelocity", ck_vector(entry->motion.angular_velocity));
    if (!entry->motion.kind_first) ck_set(value, "kind", ck_str(entry->motion.kind));
    ck_set(value, "gravity", ck_num(entry->motion.gravity));
    ck_set(value, "gravityVector", ck_vector(entry->motion.gravity_vector));
    ck_set(value, "clipMask", ck_num(entry->motion.clip_mask));
    ck_set(value, "owner", ck_nullable_saved(entry->has_owner, entry->owner));
    ck_push(motions, value);
  }
  ck_set(o, "motions", motions);
  qa_save_value *flags = ck_arr();
  for (size_t i = 0; i < capture.flag_count; i++) {
    const qa_app_sim_physics_flags_entry *entry = &capture.flags[i];
    const qa_app_sim_shared_physics_flags *f = &entry->flags;
    qa_save_value *value = ck_obj();
    ck_set(value, "actor", ck_saved(entry->actor));
    /* `{ actor, ...flags }`: members in the record's key insertion order. */
    unsigned char keys[QA_APP_SIM_FLAG_COUNT];
    size_t key_count = qa_app_sim_physics_flags_keys(f, keys);
    for (size_t k = 0; k < key_count; k++) switch ((qa_app_sim_physics_flag_key)keys[k]) {
    case QA_APP_SIM_FLAG_TEAM_SLAVE: ck_set(value, "teamSlave", ck_bool(f->team_slave)); break;
    case QA_APP_SIM_FLAG_ALWAYS_TOUCH: ck_set(value, "alwaysTouch", ck_bool(f->always_touch)); break;
    case QA_APP_SIM_FLAG_FLY: ck_set(value, "fly", ck_bool(f->fly)); break;
    case QA_APP_SIM_FLAG_SWIM: ck_set(value, "swim", ck_bool(f->swim)); break;
    case QA_APP_SIM_FLAG_PARTIAL_GROUND: ck_set(value, "partialGround", ck_bool(f->partial_ground)); break;
    case QA_APP_SIM_FLAG_DEAD: ck_set(value, "dead", ck_bool(f->dead)); break;
    case QA_APP_SIM_FLAG_PLAYER: ck_set(value, "player", ck_bool(f->player)); break;
    case QA_APP_SIM_FLAG_WATER_LEVEL: ck_set(value, "waterLevel", ck_num(f->water_level)); break;
    case QA_APP_SIM_FLAG_WATER_TYPE: ck_set(value, "waterType", ck_num(f->water_type)); break;
    case QA_APP_SIM_FLAG_DELTA_YAW: ck_set(value, "deltaYaw", ck_num(f->delta_yaw)); break;
    case QA_APP_SIM_FLAG_ENEMY: ck_set(value, "enemy", f->enemy == NULL ? ck_null() : ck_saved(entry->enemy)); break;
    case QA_APP_SIM_FLAG_COUNT: break;
    }
    ck_push(flags, value);
  }
  ck_set(o, "flags", flags);
  qa_app_sim_physics_capture_free(&capture);
  return o;
}
/* Q1ClientVisibility.capture(): `{ ...this.state }`. */
static qa_save_value *ck_visibility_value(qa_q1_client_visibility *visibility) {
  qa_q1_client_visibility_state state = qa_q1_client_visibility_capture(visibility);
  qa_save_value *o = ck_obj();
  ck_set(o, "lastCheckSlot", ck_num(state.last_check_slot));
  ck_set(o, "lastCheckTime", ck_num(state.last_check_time));
  ck_set(o, "checkedCluster", state.has_checked_cluster ? ck_num(state.checked_cluster) : ck_null());
  return o;
}
/* Q1PlayerPunch.capture(). */
static qa_save_value *ck_punch_value(qa_app_sim_q1_player_punch *punch) {
  size_t count = 0;
  qa_app_sim_q1_punch_capture_entry *entries = qa_app_sim_q1_player_punch_capture(punch, &count);
  qa_save_value *list = ck_arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_saved(entries[i].actor));
    ck_set(entry, "angles", ck_vector(entries[i].angles));
    ck_push(list, entry);
  }
  free(entries);
  return list;
}

/* ---- nativeOriginal: the q2-native original-save provider record ---- */
/* The server state both original formats carry:
 * { configstrings: [...source.game.configstrings()], portals: [...areaPortals], cvars }. */
typedef struct ck_native_server {
  qa_app_shared_simulation *sim;
  qa_q2_classic_configstring_entry *configstrings; size_t configstring_count;
  qa_q2_classic_portal_entry *portals; size_t portal_count;
  uint8_t *cvars; size_t cvars_length;
} ck_native_server;
static void ck_native_server_capture(ck_native_server *server) {
  qa_app_shared_simulation *sim = server->sim;
  size_t count = 0;
  const qa_app_sim_configstring *strings = sim->source.classic_world != NULL
      ? qa_app_sim_classic_guest_world_configstrings(sim->source.classic_world, &count)
      : qa_app_sim_rerelease_guest_world_configstrings(sim->source.rerelease_world, &count);
  server->configstrings = calloc(count != 0 ? count : 1, sizeof *server->configstrings);
  ck_oom(server->configstrings);
  for (size_t i = 0; i < count; i++) { server->configstrings[i].index = strings[i].index; server->configstrings[i].value = strings[i].value; }
  server->configstring_count = count;
  server->portals = calloc(sim->area_portals.count != 0 ? sim->area_portals.count : 1, sizeof *server->portals);
  ck_oom(server->portals);
  for (size_t i = 0; i < sim->area_portals.count; i++) {
    server->portals[i].portal = qa_ss_map_key_at(&sim->area_portals, i)->number;
    server->portals[i].open = *(const bool *)qa_ss_map_value_at(&sim->area_portals, i);
  }
  server->portal_count = sim->area_portals.count;
  qa_save_value *state = qa_cvar_registry_capture_save_state(sim->q2_server_registry);
  ck_oom(state);
  server->cvars = qa_encode_checkpoint_value(state, &server->cvars_length);
  qa_save_value_free(state);
  ck_oom(server->cvars);
}
static void ck_native_server_free(ck_native_server *server) {
  free(server->configstrings); free(server->portals); free(server->cvars);
}
static void ck_classic_server_state(void *ctx, qa_q2_classic_original_server_state *out) {
  ck_native_server *server = ctx;
  ck_native_server_capture(server);
  out->state.configstrings = server->configstrings; out->state.configstring_count = server->configstring_count;
  out->state.portals = server->portals; out->state.portal_count = server->portal_count;
  out->cvars = (qa_byte_view){server->cvars, server->cvars_length};
}
static void ck_classic_write(void *ctx, const char *game_path, const char *level_path, bool autosave) {
  qa_app_sim_classic_guest_world_write_original(ctx, game_path, level_path, autosave);
}
static qa_provider_checkpoint ck_keep_record(qa_persist_decoded *owner, qa_provider_checkpoint record) {
  uint8_t *kept = qa_persist_decoded_array(owner, record.bytes.length, 1);
  if (record.bytes.length != 0) memcpy(kept, record.bytes.bytes, record.bytes.length);
  free((void *)record.bytes.bytes);
  record.bytes = (qa_byte_view){kept, record.bytes.length};
  return record;
}
void qa_app_shared_sim_native_original_checkpoint(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *rerelease_save,
    qa_persist_decoded *owner, qa_provider_checkpoint *out) {
  const char *map = sim->recipe.map.geometry.requested_path;
  ck_native_server server; memset(&server, 0, sizeof server); server.sim = sim;
  if (qa_ss_is(sim->source.edition, "rerelease")) {
    qa_app_sim_rerelease_guest_world *world = sim->source.rerelease_world;
    qa_app_sim_q2_rerelease_native_save save; memset(&save, 0, sizeof save);
    save.module = qa_app_sim_rerelease_guest_world_module(world);
    save.map = map; save.autosave = false;
    ck_native_server_capture(&server);
    qa_app_sim_q2_rerelease_configstring *strings = calloc(server.configstring_count != 0 ? server.configstring_count : 1, sizeof *strings);
    qa_app_sim_q2_rerelease_portal *portals = calloc(server.portal_count != 0 ? server.portal_count : 1, sizeof *portals);
    ck_oom(strings); ck_oom(portals);
    for (size_t i = 0; i < server.configstring_count; i++) { strings[i].index = server.configstrings[i].index; strings[i].value = server.configstrings[i].value; }
    for (size_t i = 0; i < server.portal_count; i++) { portals[i].portal = server.portals[i].portal; portals[i].open = server.portals[i].open; }
    save.server = (qa_app_sim_q2_rerelease_level_state){strings, server.configstring_count, portals, server.portal_count};
    save.server_cvars = (qa_byte_view){server.cvars, server.cvars_length};
    /* ...(rereleaseSave ?? source.game.writeSave(false)) */
    qa_app_sim_rerelease_guest_save written; bool wrote = rerelease_save == NULL;
    if (wrote) written = qa_app_sim_rerelease_guest_world_write_save(world, false);
    const qa_app_sim_rerelease_guest_save *game = wrote ? &written : rerelease_save;
    save.game = game->game; save.level = game->level;
    qa_app_sim_q2_rerelease_visited_level *visited = calloc(sim->source.visited.count != 0 ? sim->source.visited.count : 1, sizeof *visited);
    ck_oom(visited);
    for (size_t i = 0; i < sim->source.visited.count; i++) {
      const qa_app_sim_q2_rerelease_visited_level *level = *(const qa_app_sim_q2_rerelease_visited_level *const *)qa_ss_map_value_at(&sim->source.visited, i);
      if (!qa_ss_is(level->map, map)) visited[save.visited_level_count++] = *level;
    }
    save.visited_levels = visited;
    qa_provider_checkpoint record;
    qa_app_sim_encode_q2_rerelease_native_save(&save, &record);
    *out = ck_keep_record(owner, record);
    free(visited); free(strings); free(portals);
    if (wrote) {
      for (int part = 0; part < 2; part++) {
        qa_compat_q2_rerelease_source_save *written_save = part == 0 ? &written.game : &written.level;
        free(written_save->native);
        qa_compat_q2_rerelease_deferred_damage_free_saves((qa_compat_q2_rerelease_deferred_damage_save *)written_save->deferred_damage,
            written_save->deferred_damage_count);
      }
    }
    ck_native_server_free(&server);
  } else {
    qa_app_sim_classic_guest_world *world = sim->source.classic_world;
    qa_q2_classic_original_identity identity = {qa_app_sim_classic_guest_world_module(world), map};
    qa_q2_classic_original_save captured;
    if (!qa_classic_original_save_files_capture(sim->source.files, identity, ck_classic_server_state, &server,
        ck_classic_write, world, false, &captured)) qa_range_error("Out of memory");
    qa_q2_classic_visited_level *visited = calloc(sim->source.visited.count != 0 ? sim->source.visited.count : 1, sizeof *visited);
    ck_oom(visited);
    size_t count = 0;
    for (size_t i = 0; i < sim->source.visited.count; i++) {
      const qa_q2_classic_visited_level *level = *(const qa_q2_classic_visited_level *const *)qa_ss_map_value_at(&sim->source.visited, i);
      if (!qa_ss_is(level->map, map)) visited[count++] = *level;
    }
    captured.visited_levels = visited; captured.visited_level_count = count;
    qa_provider_checkpoint record;
    if (!qa_encode_q2_classic_original_save(&captured, &record)) qa_range_error("Out of memory");
    *out = ck_keep_record(owner, record);
    /* The visited levels stay with their travel owner. */
    captured.visited_levels = NULL; captured.visited_level_count = 0;
    free(visited);
    qa_q2_classic_original_save_free(&captured);
    ck_native_server_free(&server);
  }
}

/* ---- checkpointFromSource ---- */
bool qa_app_shared_sim_native_has_source_inventory(qa_app_shared_simulation *sim) {
  return sim->source.classic_world ? qa_app_sim_classic_guest_services_has_source_inventory(qa_app_sim_classic_guest_world_services(sim->source.classic_world))
    : qa_app_sim_rerelease_guest_services_has_source_inventory(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world));
}
const char *qa_app_shared_sim_qvm_declaration(qa_app_shared_simulation *sim) {
  return sim->options.q3_guest == NULL ? NULL : qa_app_prepared_q3_primary_declaration_digest(sim->options.q3_guest->prepared);
}
/* C-only storage of a checkpoint image. */
static void *ck_keep(qa_app_shared_sim_save *save, size_t count, size_t size) {
  return qa_persist_decoded_array(&save->owner, count, size);
}
static qa_byte_view ck_keep_bytes(qa_app_shared_sim_save *save, uint8_t *bytes, size_t length) {
  ck_oom(bytes);
  uint8_t *kept = ck_keep(save, length, 1);
  memcpy(kept, bytes, length);
  free(bytes);
  return (qa_byte_view){kept, length};
}
static qa_byte_view ck_keep_value(qa_app_shared_sim_save *save, qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  return ck_keep_bytes(save, bytes, length);
}
typedef struct ck_providers { qa_provider_checkpoint *items; size_t count, capacity; } ck_providers;
static void ck_add_provider(ck_providers *list, qa_provider_checkpoint provider) {
  if (list->count == list->capacity) {
    size_t grown = list->capacity ? list->capacity * 2 : 16;
    qa_provider_checkpoint *next = realloc(list->items, grown * sizeof *next);
    ck_oom(next);
    list->items = next; list->capacity = grown;
  }
  list->items[list->count++] = provider;
}
/* TS add(schema, bytes): world:simulation is version 11, the rest 1. */
static void ck_add(ck_providers *list, qa_app_shared_sim_save *save, qa_provider_id provider, const char *schema, qa_save_value *value) {
  qa_provider_checkpoint record;
  record.provider = provider; record.schema = schema;
  record.version = strcmp(schema, "world:simulation") == 0 ? 11 : 1;
  record.bytes = ck_keep_value(save, value);
  ck_add_provider(list, record);
}
static void ck_add_bytes(ck_providers *list, qa_app_shared_sim_save *save, qa_provider_id provider, const char *schema, uint8_t *bytes, size_t length) {
  qa_provider_checkpoint record;
  record.provider = provider; record.schema = schema; record.version = 1;
  record.bytes = ck_keep_bytes(save, bytes, length);
  ck_add_provider(list, record);
}
static qa_save_value *ck_actor_map(qa_ss_map *map, bool owned, const char *field, qa_save_value *(*value)(const void *item)) {
  qa_save_value *list = ck_arr();
  for (size_t i = 0; i < map->count; i++) {
    const qa_ss_key *key = qa_ss_map_key_at(map, i);
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_actor(owned ? key->owned.id : key->actor));
    ck_set(entry, field, value(qa_ss_map_value_at(map, i)));
    ck_push(list, entry);
  }
  return list;
}
/* The checkpointProviders sink: add(schema, bytes) and providers.push into the image's provider list. */
typedef struct ck_sink { ck_providers *list; qa_app_shared_sim_save *save; qa_provider_id provider; } ck_sink;
static void ck_sink_add(void *ctx, const char *schema, uint8_t *bytes, size_t length) {
  ck_sink *sink = ctx;
  qa_provider_checkpoint record;
  record.provider = sink->provider; record.schema = schema;
  record.version = strcmp(schema, "world:simulation") == 0 ? 11 : 1;
  record.bytes = ck_keep_bytes(sink->save, bytes, length);
  ck_add_provider(sink->list, record);
}
static void ck_sink_push(void *ctx, qa_provider_checkpoint record) {
  ck_sink *sink = ctx;
  record.bytes = ck_keep_bytes(sink->save, (uint8_t *)record.bytes.bytes, record.bytes.length);
  size_t schema_length = strlen(record.schema) + 1;
  char *schema = ck_keep(sink->save, schema_length, 1);
  memcpy(schema, record.schema, schema_length);
  free((void *)record.schema);
  record.schema = schema;
  ck_add_provider(sink->list, record);
}
/* add(schema, encodeCheckpointValue(value)). */
static void ck_sink_value(const qa_app_source_checkpoint_sink *sink, const char *schema, qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  ck_oom(bytes);
  sink->add(sink->ctx, schema, bytes, length);
}
void qa_ss_q1_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink) {
  ck_sink_value(sink, "world:source-cvars", qa_cvar_registry_capture_save_state(sim->source.q1_cvars));
  qa_persist_q1_foundation_checkpoint captured;
  ck_q1_capture(sim->source.q1_game, &captured);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q1_foundation_checkpoint(&captured, &length);
  qa_q1_foundation_checkpoint_free(&captured);
  ck_oom(bytes);
  sink->add(sink->ctx, "q1:foundation", bytes, length);
}
void qa_ss_q2_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink) {
  qa_cvar_registry *registry = sim->q2_server_registry;
  if (registry != NULL) ck_sink_value(sink, "world:source-cvars", qa_cvar_registry_capture_save_state(registry));
  size_t count = 0;
  qa_provider_checkpoint *records = qa_capture_q2_product(sim->source.product, &count);
  for (size_t i = 0; i < count; i++) sink->push(sink->ctx, records[i]);
  free(records);
}
/* TS host.q3RuntimeBytes(). */
static qa_save_value *ck_q3_runtime_value(qa_app_shared_simulation *sim) {
  qa_save_value *runtime = ck_obj(), *arsenals = ck_arr(), *movement = ck_arr(), *attacks = ck_arr();
  for (size_t i = 0; i < sim->q3_arsenals.count; i++) {
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->q3_arsenals, i)->owned.id));
    ck_set(entry, "state", qa_app_q3_arsenal_runtime_state_value(qa_ss_map_value_at(&sim->q3_arsenals, i)));
    ck_push(arsenals, entry);
  }
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->player_states, i)->owned.id));
    ck_set(entry, "state", qa_app_sim_movement_player_source_movement_value(player));
    ck_push(movement, entry);
  }
  for (size_t i = 0; i < sim->last_attack.count; i++) {
    qa_save_value *entry = ck_obj();
    ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->last_attack, i)->owned.id));
    ck_set(entry, "attack", qa_app_sim_save_q2_attack_value(qa_ss_map_value_at(&sim->last_attack, i)));
    ck_push(attacks, entry);
  }
  ck_set(runtime, "arsenals", arsenals); ck_set(runtime, "movement", movement); ck_set(runtime, "lastAttacks", attacks);
  return runtime;
}
void qa_ss_q3_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink) {
  size_t length = 0;
  uint8_t *bytes = qa_app_q3_source_runtime_capture_native_bytes(sim->source.q3, &length);
  ck_oom(bytes);
  sink->add(sink->ctx, "q3:native", bytes, length);
  ck_sink_value(sink, "world:q3-runtime", ck_q3_runtime_value(sim));
}
void qa_ss_q2_native_checkpoint_extras(qa_app_shared_simulation *sim, qa_save_value *record) {
  if (qa_app_shared_sim_native_has_source_inventory(sim)) ck_set(record, "nativeInventoryProjection", ck_num(1));
}
void qa_ss_q3_qvm_checkpoint_extras(qa_app_shared_simulation *sim, qa_save_value *record) {
  const char *digest = qa_app_shared_sim_qvm_declaration(sim);
  ck_set(record, "qvmPrimaryDeclaration", digest == NULL ? ck_null() : ck_str(digest));
  if (sim->source.qvm_combat != NULL) ck_set(record, "qvmArmorProjection", ck_num(1));
  if (sim->source.qvm_inventory != NULL) ck_set(record, "qvmInventoryProjection", ck_num(1));
}
static qa_save_value *ck_double_item(const void *item) { return ck_num(*(const double *)item); }
static qa_save_value *ck_vector_item(const void *item) { return ck_vector(*(const qa_vec3 *)item); }

void qa_app_shared_sim_checkpoint_from_source(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *rerelease_save,
    const qa_save_value *weapon_behaviors, const qa_mod_session_checkpoint *mods, qa_app_shared_sim_save *out) {
  qa_app_shared_sim_assert_checkpoint_ready(sim);
  size_t enabled_count = 0;
  if (mods == NULL && sim->mod_owner != NULL && (qa_session_mods_enabled(sim->mod_owner, &enabled_count), enabled_count != 0))
    ck_fail("Selected gameplay mods require asynchronous checkpointLoading");
  sim->strategy->require_loaded(sim, "The selected source world has not completed loading");
  memset(out, 0, sizeof *out);
  ck_providers providers = {0};
  qa_provider_checkpoint native_original;
  bool has_native_original = sim->strategy->original_save(sim, rerelease_save, &out->owner, &native_original);
  qa_provider_id provider = sim->recipe.map.entities.provider;
  size_t player_count = sim->player_states.count;
  for (size_t i = 0; i < player_count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    player->arsenal = qa_app_shared_sim_arsenal(sim, player);
  }
  /* Projected selected monsters (guest proxies) are rebuilt on restore, not checkpointed. */
  qa_ss_map projected; qa_ss_map_init(&projected, QA_SS_KEY_ACTOR, 0);
  if (sim->strategy->projects_monsters(sim) && sim->selected_monsters != NULL) {
    size_t count = qa_app_sim_selected_monsters_count(sim->selected_monsters);
    for (size_t i = 0; i < count; i++) qa_ss_map_set(&projected, &qa_app_sim_selected_monsters_at(sim->selected_monsters, i)->base.actor.id, NULL);
  }
  {
    size_t count = 0;
    const qa_source_actor_checkpoint *sources = qa_session_actor_registry_source_checkpoint(sim->actors, &count);
    qa_source_actor_checkpoint *kept = ck_keep(out, count, sizeof *kept);
    size_t kept_count = 0;
    for (size_t i = 0; i < count; i++) {
      qa_actor_id id; memset(&id, 0, sizeof id);
      id.slot = (uint32_t)sources[i].actor.slot; id.generation = (uint32_t)sources[i].actor.generation;
      if (!qa_ss_map_has(&projected, &id)) kept[kept_count++] = sources[i];
    }
    qa_provider_checkpoint record;
    if (!qa_source_actors_checkpoint(kept, kept_count, &record)) qa_range_error("Out of memory");
    record.bytes = ck_keep_bytes(out, (uint8_t *)record.bytes.bytes, record.bytes.length);
    ck_add_provider(&providers, record);
  }
  qa_ss_map_free(&projected);
  {
    qa_provider_checkpoint record;
    ck_hosts hosts; ck_hosts_init(&hosts, sim);
    if (!qa_capture_primary_protection(&hosts.actors, &hosts.combat, &record)) qa_range_error("Out of memory");
    record.bytes = ck_keep_bytes(out, (uint8_t *)record.bytes.bytes, record.bytes.length);
    ck_add_provider(&providers, record);
    if (!qa_capture_source_items(&hosts.actors, &hosts.inventory, &record)) qa_range_error("Out of memory");
    ck_hosts_free(&hosts);
    record.bytes = ck_keep_bytes(out, (uint8_t *)record.bytes.bytes, record.bytes.length);
    ck_add_provider(&providers, record);
  }
  if (has_native_original) ck_add_provider(&providers, native_original);
  qa_save_value *bots = qa_app_sim_bot_services_checkpoint(&sim->bot_services);
  if (bots != NULL) {
    qa_app_sim_simulation_events_assert_output_consumed(sim->events);
    size_t length = 0;
    uint8_t *bytes = qa_encode_checkpoint_value(bots, &length);
    ck_add_bytes(&providers, out, provider, "world:bots", bytes, length);
  }
  {
    ck_sink sink = {&providers, out, provider};
    qa_app_source_checkpoint_sink add = {&sink, ck_sink_add, ck_sink_push};
    sim->strategy->checkpoint_providers(sim, &add);
  }
  qa_guest_checkpoint *guests = NULL; size_t guest_count = 0;
  {
    const qa_guest_checkpoint *source_guests = sim->strategy->checkpoint_guests(sim, &guest_count);
    if (guest_count != 0) {
      guests = ck_keep(out, guest_count, sizeof *guests);
      memcpy(guests, source_guests, guest_count * sizeof *guests);
    }
  }

  qa_save_value *record = ck_obj();
  {
    qa_save_value *settings = ck_obj();
    ck_set(settings, "skill", ck_num(sim->options.skill));
    ck_set(settings, "mode", ck_str(sim->options.mode));
    ck_set(settings, "maxClients", ck_num(sim->options.max_clients));
    ck_set(settings, "seed", ck_num(sim->options.seed));
    ck_set(settings, "startItems", ck_str(sim->start_items == NULL ? "" : sim->start_items));
    ck_set(settings, "initialSpawnPoint", ck_str(sim->initial_spawn_point == NULL ? "" : sim->initial_spawn_point));
    ck_set(record, "settings", settings);
  }
  ck_set(record, "modClientApplicationOrdinal", ck_num((double)qa_mod_client_applications_checkpoint(sim->mod_client_applications)));
  ck_set(record, "q1Punch", ck_punch_value(sim->q1_punch));
  sim->strategy->checkpoint_extras(sim, record);
  {
    qa_save_value *players = ck_arr();
    for (size_t i = 0; i < player_count; i++)
      ck_push(players, qa_app_sim_capture_movement_player(*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i)));
    ck_set(record, "players", players);
  }
  ck_set(record, "hostMilliseconds", ck_num(sim->host_milliseconds));
  ck_set(record, "q1Paused", ck_bool(sim->q1_pause_state));
  {
    qa_save_value *commands = ck_arr();
    for (size_t i = 0; i < sim->mod_client_commands.count; i++) {
      const qa_mod_client_command_receipt *value = qa_ss_map_value_at(&sim->mod_client_commands, i);
      qa_client_id client;
      if (!qa_app_shared_sim_player_client(sim, value->input.actor, &client)) ck_fail("Accepted command lost its live client");
      ck_push(commands, qa_app_sim_capture_mod_client_command(value, client));
    }
    ck_set(record, "modClientCommands", commands);
  }
  ck_set(record, "sourceSchedulingMilliseconds", ck_num(sim->source_scheduling_milliseconds));
  {
    qa_app_source_native_client *clients = NULL; size_t count = 0;
    if (sim->strategy->native_clients(sim, &clients, &count)) {
      qa_save_value *list = ck_arr();
      for (size_t i = 0; i < count; i++) {
        qa_save_value *entry = ck_obj();
        ck_set(entry, "clientSlot", ck_num(clients[i].client_slot));
        ck_set(entry, "phase", ck_str(clients[i].phase));
        ck_set(entry, "userinfo", ck_str(clients[i].userinfo));
        ck_push(list, entry);
      }
      ck_set(record, "nativeClients", list);
      free(clients);
    } else ck_set(record, "nativeClients", ck_null());
  }
  ck_set(record, "attackSequence", ck_num(sim->attack_sequence));
  ck_set(record, "q1ClientVisibility", ck_visibility_value(sim->q1_client_visibility));
  {
    qa_save_value *list = ck_arr();
    if (bots == NULL) {
      size_t count = 0;
      qa_cvar_snapshot *snapshots = sim->strategy->source_cvar_snapshots(sim, &count);
      for (size_t i = 0; i < count; i++) {
        qa_save_value *entry = ck_obj();
        ck_set(entry, "name", ck_str(snapshots[i].name));
        ck_set(entry, "value", ck_str(snapshots[i].value));
        ck_push(list, entry);
      }
      for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
      free(snapshots);
    }
    ck_set(record, "sourceCvars", list);
  }
  qa_save_value_free(bots);
  {
    qa_save_value *campaign = ck_obj();
    ck_set(campaign, "flags", ck_num(sim->q1_campaign.flags));
    ck_set(campaign, "skill", ck_num(sim->q1_campaign.skill));
    ck_set(record, "campaign", campaign);
  }
  ck_set(record, "physics", ck_physics_value(sim->physics));
  ck_set(record, "events", qa_app_shared_sim_events_capture(sim));
  {
    qa_save_value *portals = ck_arr();
    for (size_t i = 0; i < sim->area_portals.count; i++) {
      qa_save_value *entry = ck_obj();
      ck_set(entry, "portal", ck_num(qa_ss_map_key_at(&sim->area_portals, i)->number));
      ck_set(entry, "open", ck_bool(*(const bool *)qa_ss_map_value_at(&sim->area_portals, i)));
      ck_push(portals, entry);
    }
    ck_set(record, "portals", portals);
  }
  if (sim->selected_q3_source == NULL) ck_set(record, "selectedQ3Source", ck_null());
  else {
    qa_save_value *selected = ck_obj();
    ck_set(selected, "source", qa_app_q3_selected_source_capture(sim->selected_q3_source));
    ck_set(selected, "milliseconds", ck_num(sim->selected_milliseconds));
    ck_set(selected, "time", ck_num(sim->selected_q3_time));
    ck_set(selected, "next", ck_num(sim->selected_q3_next));
    qa_save_value *strings = ck_arr();
    for (size_t i = 0; i < sim->selected_q3_strings.count; i++) {
      qa_save_value *entry = ck_obj();
      ck_set(entry, "index", ck_num(qa_ss_map_key_at(&sim->selected_q3_strings, i)->number));
      ck_set(entry, "value", ck_str(*(char **)qa_ss_map_value_at(&sim->selected_q3_strings, i)));
      ck_push(strings, entry);
    }
    ck_set(selected, "strings", strings);
    ck_set(record, "selectedQ3Source", selected);
  }
  if (sim->hand_grenades == NULL) ck_set(record, "handGrenades", ck_null());
  else {
    qa_app_sim_hand_grenade_runtime_checkpoint captured = qa_app_sim_hand_grenade_runtime_capture(sim->hand_grenades);
    ck_set(record, "handGrenades", qa_app_sim_hand_grenade_runtime_checkpoint_value(&captured));
    qa_app_sim_hand_grenade_runtime_checkpoint_destroy(&captured);
  }
  if (sim->grapple == NULL) ck_set(record, "grapple", ck_null());
  else {
    qa_app_sim_grapple_runtime_checkpoint captured = qa_app_sim_grapple_runtime_capture(sim->grapple);
    ck_set(record, "grapple", qa_app_sim_grapple_runtime_checkpoint_value(&captured));
    qa_app_sim_grapple_runtime_checkpoint_destroy(&captured);
  }
  {
    qa_save_value *slots = ck_arr();
    for (size_t i = 0; i < sim->weapon_slots.count; i++) {
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->weapon_slots, i)->actor));
      ck_set(entry, "state", qa_app_sim_weapon_slot_state_value(qa_app_sim_weapon_slot_snapshot(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i))));
      ck_push(slots, entry);
    }
    ck_set(record, "weaponSlots", slots);
  }
  ck_set(record, "droppedPickups", qa_app_sim_source_pickup_cargo_capture(sim->dropped_pickups));
  ck_set(record, "nativeEquipmentVelocity", ck_actor_map(&sim->native_equipment_velocity, false, "velocity", ck_vector_item));
  ck_set(record, "nativeWeaponRequests", ck_actor_map(&sim->native_weapon_requests, false, "weapon", ck_double_item));
  {
    qa_save_value *list = ck_arr();
    if (sim->native_primary_inventory != NULL) {
      size_t count = 0;
      const qa_actor_id *actors = qa_app_shared_sim_players(sim, &count);
      for (size_t i = 0; i < count; i++) {
        qa_compat_q2_native_inventory_readout readout;
        if (!qa_compat_q2_native_primary_inventory_read(sim->native_primary_inventory, actors[i], &readout)) continue;
        qa_save_value *entry = ck_obj();
        ck_set(entry, "actor", ck_actor(actors[i]));
        ck_set(entry, "item", readout.selected == NULL ? ck_null() : ck_str(readout.selected));
        ck_push(list, entry);
      }
    }
    ck_set(record, "nativeInventorySelections", list);
  }
  ck_set(record, "nativeWeaponTimes", ck_actor_map(&sim->native_weapon_times, false, "time", ck_double_item));
  ck_set(record, "selectedMonsters", qa_app_shared_sim_capture_selected_monsters(sim));
  {
    qa_app_sim_foreign_q2_world_checkpoint foreign;
    if (qa_app_shared_sim_capture_foreign_world(sim, &foreign)) {
      ck_set(record, "foreignWorld", qa_app_sim_foreign_q2_world_checkpoint_value(&foreign));
      qa_app_shared_sim_foreign_world_checkpoint_free(&foreign);
    } else ck_set(record, "foreignWorld", ck_null());
  }
  ck_set(record, "foreignQ1World", qa_app_shared_sim_capture_foreign_q1_world(sim));
  if (weapon_behaviors != NULL) {
    size_t length = 0;
    ck_set(record, "weaponBehaviors", ck_tree(qa_encode_checkpoint_value(weapon_behaviors, &length), &length));
  } else {
    qa_app_sim_weapon_behavior_runtime_checkpoint behaviors = qa_app_sim_simulation_weapon_behaviors_checkpoint(sim->weapon_behavior);
    ck_set(record, "weaponBehaviors", behaviors.value);
  }
  if (sim->selected_ammo_timers == NULL) ck_set(record, "selectedAmmoTimers", ck_null());
  else {
    size_t count = 0;
    const qa_owned_actor *actors = qa_app_shared_sim_selected_ammo_actors(sim, &count);
    ck_set(record, "selectedAmmoTimers", qa_app_q3_mapped_ammo_regeneration_capture(sim->selected_ammo_timers, actors, count));
  }
  if (!sim->has_selected_weapon_source) ck_set(record, "selectedWeaponSource", ck_null());
  else {
    qa_ss_selected_weapon_source *weapon = &sim->selected_weapon_source;
    qa_save_value *source = ck_obj();
    ck_set(source, "kind", ck_str(weapon->kind));
    if (qa_ss_is(weapon->kind, "q1")) {
      ck_set(source, "entities", ck_q1_capture_value(weapon->q1_game));
      ck_set(source, "random", ck_random_value(weapon->random));
    } else {
      qa_q2_foundation_checkpoint entities = qa_q2_game_services_capture(weapon->q2_game);
      size_t length = 0;
      ck_set(source, "entities", ck_tree(qa_encode_q2_foundation_checkpoint(&entities, &length), &length));
      qa_q2w_weapons_checkpoint weapons;
      if (!qa_q2w_weapons_capture(weapon->weapons, weapon->q2_game, &weapons)) qa_range_error("Out of memory");
      ck_set(source, "weapons", ck_tree(qa_encode_q2_weapons_checkpoint(&weapons, &length), &length));
      qa_q2w_weapons_checkpoint_free(&weapons);
      ck_set(source, "random", ck_random_value(weapon->random));
      ck_set(source, "frame", qa_save_frame_value(weapon->frame));
      ck_set(source, "mapMilliseconds", ck_num(weapon->map_milliseconds));
      ck_set(source, "nextMilliseconds", ck_num(weapon->next_milliseconds));
      ck_set(source, "intervalMilliseconds", ck_num(weapon->interval_milliseconds));
      qa_save_value *turns = ck_arr();
      size_t count = 0;
      const qa_actor_id *actors = qa_app_shared_sim_players(sim, &count);
      for (size_t i = 0; i < count; i++) {
        if (!sim->has_selected_arsenal || !qa_ss_is(sim->selected_arsenal.family, "q2")) ck_fail("Selected Q2 source has no arsenal");
        qa_app_q2_selected_weapon_turn_state turn = qa_app_q2_selected_arsenal_capture_turn(sim->selected_arsenal.q2, actors[i]);
        qa_save_value *entry = ck_obj(), *state = ck_obj();
        ck_set(entry, "actor", ck_actor(actors[i]));
        ck_set(state, "buttons", ck_num(turn.turn.buttons));
        ck_set(state, "latchedButtons", ck_num(turn.turn.latched_buttons));
        ck_set(state, "weaponThunk", ck_bool(turn.turn.weapon_thunk));
        if (turn.has_firing) {
          qa_save_value *firing = ck_obj();
          ck_set(firing, "weapon", ck_str(turn.firing_weapon));
          ck_set(firing, "credit", ck_num(turn.firing_credit));
          ck_set(state, "firing", firing);
        } else ck_set(state, "firing", ck_null());
        ck_set(entry, "state", state);
        ck_push(turns, entry);
      }
      ck_set(source, "turns", turns);
    }
    ck_set(record, "selectedWeaponSource", source);
  }
  if (!sim->has_selected_arsenal || !qa_ss_is(sim->selected_arsenal.family, "q3")) ck_set(record, "selectedArsenals", ck_null());
  else {
    qa_save_value *list = ck_arr();
    size_t count = 0;
    const qa_actor_id *actors = qa_app_shared_sim_players(sim, &count);
    for (size_t i = 0; i < count; i++) {
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(actors[i]));
      qa_app_q3_selected_arsenal_checkpoint state = qa_app_q3_selected_arsenal_capture(sim->selected_arsenal.q3, actors[i]);
      ck_set(entry, "state", qa_app_q3_selected_arsenal_checkpoint_value(&state));
      ck_push(list, entry);
    }
    ck_set(record, "selectedArsenals", list);
  }
  ck_set(record, "q1CharacterFoundation", sim->q1_character_foundation == NULL ? ck_null() : ck_q1_capture_value(sim->q1_character_foundation));
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->q1_characters.count; i++) {
      size_t length = 0;
      uint8_t *bytes = qa_q1_character_actor_capture(*(qa_q1_character_actor **)qa_ss_map_value_at(&sim->q1_characters, i), &length);
      ck_oom(bytes);
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->q1_characters, i)->owned.id));
      ck_set(entry, "bytes", qa_save_value_bytes(bytes, length));
      free(bytes);
      ck_push(list, entry);
    }
    ck_set(record, "q1Characters", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->q2_characters.count; i++) {
      qa_q2_base_character_checkpoint state = qa_q2_character_actor_capture(*(qa_q2_character_actor **)qa_ss_map_value_at(&sim->q2_characters, i));
      size_t length = 0;
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->q2_characters, i)->owned.id));
      ck_set(entry, "state", ck_tree(qa_encode_q2_character_checkpoint(&state, &length), &length));
      ck_push(list, entry);
    }
    ck_set(record, "q2Characters", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->characters.count; i++) {
      qa_q3_character_checkpoint state = qa_q3_character_actor_capture(*(qa_q3_character_actor **)qa_ss_map_value_at(&sim->characters, i));
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->characters, i)->owned.id));
      ck_set(entry, "state", qa_q3_character_checkpoint_value(&state));
      ck_push(list, entry);
    }
    ck_set(record, "q3Characters", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->character_starts.count; i++) {
      const qa_ss_optional_actor *start = qa_ss_map_value_at(&sim->character_starts, i);
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->character_starts, i)->owned.id));
      ck_set(entry, "start", start->has ? ck_actor(start->actor) : ck_null());
      ck_push(list, entry);
    }
    ck_set(record, "characterStarts", list);
  }
  {
    qa_q3_death_animation_checkpoint death = qa_q3_death_animation_sequence_capture(&sim->death_animations);
    qa_save_value *value = ck_obj();
    ck_set(value, "version", ck_num(death.version));
    ck_set(value, "index", ck_num(death.index));
    ck_set(record, "deathAnimations", value);
  }
  ck_set(record, "characterTicks", ck_actor_map(&sim->character_ticks, true, "time", ck_double_item));
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->entry_carry.count; i++) {
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->entry_carry, i)->owned.id));
      ck_set(entry, "state", qa_q1_travel_state_value(qa_ss_map_value_at(&sim->entry_carry, i)));
      ck_push(list, entry);
    }
    ck_set(record, "entryCarry", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->detached_models.count; i++) {
      const qa_ss_detached_model *model = qa_ss_map_value_at(&sim->detached_models, i);
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->detached_models, i)->owned.id));
      ck_set(entry, "content", ck_str(model->content));
      ck_set(entry, "path", ck_str(model->path));
      if (model->family != NULL) ck_set(entry, "family", ck_str(model->family));
      if (model->has_skin) ck_set(entry, "skin", ck_num(model->skin));
      if (model->has_effects) ck_set(entry, "effects", ck_num(model->effects));
      ck_push(list, entry);
    }
    ck_set(record, "detachedModels", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->source_models.count; i++)
      ck_push(list, qa_q2_presentation_model_value_ordered(qa_ss_map_value_at(&sim->source_models, i),
          qa_ss_map_has(&sim->source_model_read_order, &qa_ss_map_key_at(&sim->source_models, i)->actor)));
    ck_set(record, "sourceModels", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->view_models.count; i++) {
      const qa_ss_view_model *model = qa_ss_map_value_at(&sim->view_models, i);
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->view_models, i)->actor));
      ck_set(entry, "path", ck_str(model->path));
      ck_set(entry, "frame", ck_num(model->frame));
      if (model->has_q2) {
        qa_save_value *q2 = ck_obj();
        ck_set(q2, "skin", ck_num(model->q2_skin));
        ck_set(q2, "rate", ck_num(model->q2_rate));
        ck_set(q2, "kickOrigin", ck_vector(model->q2_kick_origin));
        ck_set(q2, "kickAngles", ck_vector(model->q2_kick_angles));
        ck_set(entry, "q2", q2);
      } else ck_set(entry, "q2", ck_null());
      ck_push(list, entry);
    }
    ck_set(record, "viewModels", list);
  }
  {
    qa_save_value *list = ck_arr();
    for (size_t i = 0; i < sim->q2_views.count; i++) {
      qa_save_value *entry = ck_obj();
      ck_set(entry, "actor", ck_actor(qa_ss_map_key_at(&sim->q2_views, i)->actor));
      ck_set(entry, "view", qa_q2_player_view_value(qa_ss_map_value_at(&sim->q2_views, i)));
      ck_push(list, entry);
    }
    ck_set(record, "q2Views", list);
  }
  ck_set(record, "levelChange", ck_null());
  ck_add(&providers, out, provider, "world:simulation", record);

  /* The image (SaveImage key order of checkpointFromSource). */
  qa_save_image *image = &out->image;
  image->schema_version = 4;
  image->key_order = QA_SAVE_IMAGE_ORDER_CHECKPOINT;
  image->recipe = sim->recipe;
  image->frame = sim->source_frame;
  image->next_event_sequence = qa_app_sim_simulation_events_next_sequence(sim->events);
  if (mods != NULL) {
    qa_mod_session_checkpoint *kept = ck_keep(out, 1, sizeof *kept);
    *kept = *mods;
    image->mods = kept;
  }
  {
    qa_app_shared_sim_consumption consumption = qa_app_shared_sim_selection_consumption(sim);
    qa_selection_consumption_entry *entries = ck_keep(out, consumption.count, sizeof *entries);
    for (size_t i = 0; i < consumption.count; i++) {
      entries[i].axis = consumption.entries[i].axis;
      entries[i].requested = consumption.entries[i].requested;
      entries[i].outcome = consumption.entries[i].outcome;
      size_t length = strlen(consumption.entries[i].detail) + 1;
      char *detail = ck_keep(out, length, 1);
      memcpy(detail, consumption.entries[i].detail, length);
      entries[i].detail = detail;
    }
    image->consumption = entries; image->consumption_count = consumption.count;
    qa_app_shared_sim_selection_consumption_free(&consumption);
  }
  qa_save_clock *clocks = ck_keep(out, 1, sizeof *clocks);
  clocks[0].provider = provider; clocks[0].time = sim->source_frame.time;
  image->clocks = clocks; image->clock_count = 1;
  {
    qa_save_value *state = ck_random_value(sim->random);
    qa_save_reader reader;
    if (!qa_save_reader_init(&reader, state, "random")) qa_range_error("Out of memory");
    qa_save_random *random = ck_keep(out, 1, sizeof *random);
    random[0].provider = provider;
    random[0].state = qa_read_random(&reader);
    qa_save_reader_destroy(&reader);
    if (random[0].state.words.count != 0) {
      double *words = ck_keep(out, random[0].state.words.count, sizeof *words);
      memcpy(words, random[0].state.words.values, random[0].state.words.count * sizeof *words);
      free((void *)random[0].state.words.values);
      random[0].state.words.values = words;
    }
    qa_save_value_free(state);
    image->random = random; image->random_count = 1;
  }
  {
    size_t count = 0;
    const qa_actor_slot_checkpoint *slots = qa_session_actor_registry_checkpoint(sim->actors, &count);
    qa_actor_slot_checkpoint *kept = ck_keep(out, count, sizeof *kept);
    if (count != 0) memcpy(kept, slots, count * sizeof *kept);
    image->actors = kept; image->actor_count = count;
  }
  {
    qa_body_checkpoint *bodies = NULL; size_t count = 0;
    ck_hosts hosts; ck_hosts_init(&hosts, sim);
    if (!qa_capture_shared_bodies(&hosts.actors, &hosts.bodies, &bodies, &count)) qa_range_error("Out of memory");
    ck_hosts_free(&hosts);
    out->bodies = bodies; out->body_count = count;
    image->bodies = bodies; image->body_count = count;
  }
  size_t observation_count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  qa_combat_checkpoint *combat = ck_keep(out, observation_count, sizeof *combat);
  qa_inventory_checkpoint *inventories = ck_keep(out, observation_count, sizeof *inventories);
  qa_think_checkpoint *thinks = ck_keep(out, observation_count, sizeof *thinks);
  size_t combat_count = 0, inventory_count = 0, think_count = 0;
  for (size_t i = 0; i < observation_count; i++) {
    qa_actor_id actor = observations[i].id;
    if (!qa_ss_map_has(&sim->native_mirrored_combat, &actor)) {
      qa_combat_state_result state = qa_gameplay_authority_read(sim->combat, actor);
      if (state.has_value) { combat[combat_count].actor = qa_saved_actor_id_of(actor); combat[combat_count].state = state.value; combat_count++; }
    }
    if (qa_shared_inventory_table_has(sim->inventory, actor)) {
      size_t count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
      qa_inventory_entry *kept = ck_keep(out, count, sizeof *kept);
      if (count != 0) memcpy(kept, entries, count * sizeof *kept);
      inventories[inventory_count].actor = qa_saved_actor_id_of(actor);
      inventories[inventory_count].entries = kept; inventories[inventory_count].entry_count = count;
      inventory_count++;
    }
    qa_scheduled_think_result pending = qa_frame_scheduler_pending(sim->scheduler, actor);
    if (pending.has_value) {
      qa_think_checkpoint *think = &thinks[think_count++];
      think->actor = qa_saved_actor_id_of(actor);
      think->callback = pending.value.callback;
      think->has_execution_provider = pending.value.timing.has_execution_provider;
      think->execution_provider = pending.value.timing.execution_provider;
      think->due = pending.value.timing.due;
      think->boundary = pending.value.timing.boundary;
      think->provider = pending.value.timing.order.provider;
      think->sequence = pending.value.timing.order.sequence;
    }
  }
  image->combat = combat; image->combat_count = combat_count;
  image->inventories = inventories; image->inventory_count = inventory_count;
  image->thinks = thinks; image->think_count = think_count;
  {
    size_t count = 0;
    const qa_actor_id *actors = qa_app_shared_sim_players(sim, &count);
    qa_actor_configuration_checkpoint *configurations = ck_keep(out, count, sizeof *configurations);
    for (size_t i = 0; i < count; i++) {
      configurations[i].actor = qa_saved_actor_id_of(actors[i]);
      configurations[i].movement = sim->recipe.movement;
      configurations[i].character = sim->recipe.character;
      configurations[i].weapons = sim->recipe.weapons;
      configurations[i].inventory = sim->recipe.inventory;
    }
    image->configurations = configurations; image->configuration_count = count;
  }
  qa_provider_checkpoint *kept = ck_keep(out, providers.count, sizeof *kept);
  if (providers.count != 0) memcpy(kept, providers.items, providers.count * sizeof *kept);
  free(providers.items);
  image->providers = kept; image->provider_count = providers.count;
  image->guests = guests; image->guest_count = guest_count;
}

void qa_app_shared_sim_save_free(qa_app_shared_sim_save *save) {
  if (save == NULL) return;
  qa_body_checkpoints_free(save->bodies, save->body_count);
  qa_persist_decoded_free(&save->owner);
  memset(save, 0, sizeof *save);
}

/* checkpoint(): checkpointFromSource() with no asynchronous captures. */
void qa_app_shared_sim_checkpoint(qa_app_shared_simulation *sim, qa_app_shared_sim_save *out) {
  qa_app_shared_sim_checkpoint_from_source(sim, NULL, NULL, NULL, out);
}

/* checkpointLoading(nextFrame): the mod session, weapon components and the
 * rerelease native game are captured first (TS awaits them), then
 * checkpointFromSource assembles the image. */
void qa_app_shared_sim_checkpoint_loading(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx,
    qa_app_shared_sim_save *out) {
  qa_app_shared_sim_assert_checkpoint_ready(sim);
  sim->strategy->assert_savable(sim);
  sim->checkpoint_in_progress = true;
  qa_mod_session_checkpoint mods; bool has_mods = false;
  qa_mod_checkpoint *mod_entries = NULL;
  if (sim->mod_owner != NULL) {
    size_t count = 0;
    qa_session_mods_checkpoint(sim->mod_owner, &mod_entries, &count);
    mods.version = 1; mods.mods = mod_entries; mods.mod_count = count;
    has_mods = true;
  }
  qa_app_sim_weapon_behavior_runtime_checkpoint behaviors =
      qa_app_sim_simulation_weapon_behaviors_checkpoint_loading(sim->weapon_behavior, next_frame, frame_ctx);
  qa_app_sim_rerelease_guest_save captured;
  bool has_captured = sim->strategy->capture_guest_save(sim, next_frame, frame_ctx, &captured);
  sim->checkpoint_in_progress = false;
  qa_app_shared_sim_checkpoint_from_source(sim, has_captured ? &captured : NULL, behaviors.value, has_mods ? &mods : NULL, out);
  if (has_mods) {
    /* The image keeps its own copy of the record; the entries stay with it. */
    qa_mod_checkpoint *kept = qa_persist_decoded_array(&out->owner, mods.mod_count, sizeof *kept);
    if (mods.mod_count != 0) memcpy(kept, mod_entries, mods.mod_count * sizeof *kept);
    ((qa_mod_session_checkpoint *)out->image.mods)->mods = kept;
  }
  free(mod_entries);
  qa_save_value_free(behaviors.value);
  if (has_captured) {
    for (int part = 0; part < 2; part++) {
      qa_compat_q2_rerelease_source_save *written = part == 0 ? &captured.game : &captured.level;
      free(written->native);
      qa_compat_q2_rerelease_deferred_damage_free_saves((qa_compat_q2_rerelease_deferred_damage_save *)written->deferred_damage,
          written->deferred_damage_count);
    }
  }
}

/* ---- restoreNativeLoading / restore (runtime.ts) ---- */
void qa_app_shared_sim_own(qa_app_shared_simulation *sim, void (*dispose)(void *ctx), void *ctx) {
  if (sim->b2_owned_count == sim->b2_owned_capacity) {
    size_t grown = sim->b2_owned_capacity ? sim->b2_owned_capacity * 2 : 16;
    qa_ss_dispose *next = realloc(sim->b2_owned, grown * sizeof *next);
    ck_oom(next);
    sim->b2_owned = next; sim->b2_owned_capacity = grown;
  }
  sim->b2_owned[sim->b2_owned_count++] = (qa_ss_dispose){ctx, dispose};
}
static void ck_own(qa_app_shared_simulation *sim, void (*dispose)(void *ctx), void *ctx) { qa_app_shared_sim_own(sim, dispose, ctx); }
static void ck_dispose_tree(void *ctx) { qa_save_value_free(ctx); }
static void ck_dispose_classic_save(void *ctx) { qa_q2_classic_original_save_free(ctx); free(ctx); }
static void ck_dispose_rerelease_save(void *ctx) {
  qa_app_sim_q2_rerelease_native_save *save = ctx;
  qa_app_sim_q2_rerelease_native_save_free(save);
  free(save);
}
static qa_save_reader ck_f(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static qa_save_reader ck_i(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("Out of memory");
  return out;
}
static bool ck_present(const qa_save_reader *reader) { return !qa_save_reader_is_undefined(reader); }
static bool ck_given(const qa_save_reader *reader) { return !qa_save_reader_is_undefined(reader) && !qa_save_reader_is_null(reader); }
static double ck_finite_of(const qa_save_reader *r, const char *name) { qa_save_reader f = ck_f(r, name); double v = qa_save_reader_finite(&f); qa_save_reader_destroy(&f); return v; }
static double ck_number_of(const qa_save_reader *r, const char *name) { qa_save_reader f = ck_f(r, name); double v = qa_save_reader_number(&f); qa_save_reader_destroy(&f); return v; }
static double ck_integer_of(const qa_save_reader *r, const char *name, double minimum) { qa_save_reader f = ck_f(r, name); double v = qa_save_reader_integer_min(&f, minimum); qa_save_reader_destroy(&f); return v; }
static double ck_any_integer_of(const qa_save_reader *r, const char *name) { qa_save_reader f = ck_f(r, name); double v = qa_save_reader_integer(&f); qa_save_reader_destroy(&f); return v; }
static bool ck_bool_of(const qa_save_reader *r, const char *name) { qa_save_reader f = ck_f(r, name); bool v = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f); return v; }
static const char *ck_string_of(const qa_save_reader *r, const char *name) { qa_save_reader f = ck_f(r, name); const char *v = qa_save_reader_string(&f); qa_save_reader_destroy(&f); return v; }
static qa_vec3 ck_vector_of(const qa_save_reader *r, const char *name) { qa_save_reader f = ck_f(r, name); qa_vec3 v = qa_read_vector(&f); qa_save_reader_destroy(&f); return v; }

/* TS `(value: SaveReader) => this.actors.referenceSaved(readSavedActor(value))`. */
static qa_actor_id ck_reference_saved(void *ctx, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(((qa_app_shared_simulation *)ctx)->actors, saved, "checkpoint");
}
static qa_actor_id ck_reference(qa_app_shared_simulation *sim, const qa_save_reader *value) {
  return ck_reference_saved(sim, qa_read_saved_actor(value));
}
/* TS owner(value): resolveSaved or SaveFormatError "Missing restored actor". */
static qa_owned_actor ck_owner(qa_app_shared_simulation *sim, const qa_save_reader *value) {
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_saved(sim->actors, qa_read_saved_actor(value), &actor)) qa_save_reader_fail(value, "Missing restored actor");
  return actor;
}
static qa_owned_actor ck_owner_of(qa_app_shared_simulation *sim, const qa_save_reader *r, const char *name) {
  qa_save_reader f = ck_f(r, name); qa_owned_actor v = ck_owner(sim, &f); qa_save_reader_destroy(&f); return v;
}
static bool ck_same_owned(qa_owned_actor a, qa_owned_actor b) { return qa_same_owned_actor(a, b); }

/* The ClassicGuestMap TS builds for a native restore: { map, entities, spawnPoint }. */
typedef struct ck_native_map { char *map, *entities; qa_app_sim_classic_guest_map view; } ck_native_map;
static ck_native_map ck_native_map_of(qa_app_shared_simulation *sim) {
  ck_native_map out;
  out.map = qa_ss_map_name(sim->recipe.map.geometry.requested_path);
  qa_app_sim_native_q2_map_geometry geometry = {sim->world_kind, sim->world_entities,
      (double)(sim->world.kind == QA_DECODED_WORLD_Q1 ? sim->world.as.q1->model_count
               : sim->world.kind == QA_DECODED_WORLD_Q2 ? sim->world.as.q2->model_count : sim->world.as.q3->model_count)};
  char *prepared = qa_app_sim_prepare_native_q2_map(geometry, sim->source.edition, sim->options.mode);
  out.entities = qa_ss_strdup(qa_app_shared_sim_guest_monster_entities(sim, prepared, sim->source.edition));
  free(prepared);
  out.view = (qa_app_sim_classic_guest_map){out.map, out.entities, sim->initial_spawn_point};
  return out;
}
static void ck_native_map_free(ck_native_map *map) { free(map->map); free(map->entities); }

/* The restoreServerState closure: configstrings, then portals into areaPortals and the scene. */
typedef struct ck_server_restore {
  qa_app_shared_simulation *sim;
  const qa_app_sim_configstring *strings; size_t string_count;
  const double *portals; const bool *open; size_t portal_count;
} ck_server_restore;
static void ck_restore_server_state(void *ctx) {
  ck_server_restore *restore = ctx;
  qa_app_shared_simulation *sim = restore->sim;
  if (sim->source.classic_world != NULL) qa_app_sim_classic_guest_world_restore_configstrings(sim->source.classic_world, restore->strings, restore->string_count);
  else qa_app_sim_rerelease_guest_world_restore_configstrings(sim->source.rerelease_world, restore->strings, restore->string_count);
  for (size_t i = 0; i < restore->portal_count; i++) {
    qa_ss_map_set(&sim->area_portals, &restore->portals[i], &restore->open[i]);
    qa_shared_set_area_portal_state(sim->scene, restore->portals[i], restore->open[i]);
  }
}
typedef struct ck_server_storage {
  ck_server_restore restore;
  qa_app_sim_configstring *strings; double *portals; bool *open;
} ck_server_storage;
static void ck_server_storage_free(ck_server_storage *storage) { free(storage->strings); free(storage->portals); free(storage->open); }
static void ck_server_storage_init(ck_server_storage *storage, qa_app_shared_simulation *sim, size_t string_count, size_t portal_count) {
  storage->strings = calloc(string_count ? string_count : 1, sizeof *storage->strings);
  storage->portals = calloc(portal_count ? portal_count : 1, sizeof *storage->portals);
  storage->open = calloc(portal_count ? portal_count : 1, sizeof *storage->open);
  ck_oom(storage->strings); ck_oom(storage->portals); ck_oom(storage->open);
  storage->restore = (ck_server_restore){sim, storage->strings, string_count, storage->portals, storage->open, portal_count};
}

/* nativeQ2RereleaseSave(save, module), decoded in full and kept for the
 * simulation's lifetime: its visited levels stay in source.visited. */
static qa_app_sim_q2_rerelease_native_save *ck_rerelease_original(qa_app_shared_simulation *sim, const qa_save_image *save) {
  qa_module_identity module = qa_app_sim_rerelease_guest_world_module(sim->source.rerelease_world);
  qa_app_sim_rerelease_native_save_view view; memset(&view, 0, sizeof view);
  if (!qa_app_sim_native_q2_rerelease_save(*save, &module, &view)) return NULL;
  qa_save_value_free(view.value);
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(*save, "q2:rerelease-native-original");
  qa_app_sim_q2_rerelease_native_save *original = calloc(1, sizeof *original);
  ck_oom(original);
  qa_app_sim_decode_q2_rerelease_native_save(&record, (qa_app_sim_q2_rerelease_native_identity){module, sim->recipe.map.geometry.requested_path}, original);
  ck_own(sim, ck_dispose_rerelease_save, original);
  return original;
}
static qa_q2_classic_original_save *ck_classic_original(qa_app_shared_simulation *sim, const qa_save_image *save) {
  qa_module_identity module = qa_app_sim_classic_guest_world_module(sim->source.classic_world);
  qa_q2_classic_original_save *original = calloc(1, sizeof *original);
  ck_oom(original);
  if (!qa_app_sim_native_q2_original_save(*save, &module, original)) { free(original); return NULL; }
  ck_own(sim, ck_dispose_classic_save, original);
  return original;
}
static void ck_require_native_original(qa_app_shared_simulation *sim, const void *original, const char *message) {
  if (original == NULL || sim->q2_server_registry == NULL || qa_cvar_registry_variable_value(sim->q2_server_registry, "deathmatch") != 0)
    ck_fail("%s", message);
}
static void ck_require_non_deathmatch(qa_app_shared_simulation *sim) {
  if (qa_cvar_registry_variable_value(sim->q2_server_registry, "deathmatch") != 0) ck_fail("Selected DLL does not support non-deathmatch original saves");
}
static void ck_rerelease_server_storage(ck_server_storage *storage, qa_app_shared_simulation *sim, const qa_app_sim_q2_rerelease_native_save *original) {
  ck_server_storage_init(storage, sim, original->server.configstring_count, original->server.portal_count);
  for (size_t i = 0; i < original->server.configstring_count; i++) {
    storage->strings[i].index = original->server.configstrings[i].index; storage->strings[i].value = original->server.configstrings[i].value;
  }
  for (size_t i = 0; i < original->server.portal_count; i++) {
    storage->portals[i] = original->server.portals[i].portal; storage->open[i] = original->server.portals[i].open;
  }
}
static void ck_classic_server_storage(ck_server_storage *storage, qa_app_shared_simulation *sim, const qa_q2_classic_original_save *original) {
  const qa_q2_classic_level_state *state = &original->server.state;
  ck_server_storage_init(storage, sim, state->configstring_count, state->portal_count);
  for (size_t i = 0; i < state->configstring_count; i++) {
    storage->strings[i].index = state->configstrings[i].index; storage->strings[i].value = state->configstrings[i].value;
  }
  for (size_t i = 0; i < state->portal_count; i++) { storage->portals[i] = state->portals[i].portal; storage->open[i] = state->portals[i].open; }
}
/* files.restore callback: (game, level) => source.game.restoreOriginal(game, level, map, restoreServerState). */
typedef struct ck_classic_read { qa_app_shared_simulation *sim; ck_native_map *map; ck_server_storage *server; bool loading;
  void (*next_frame)(void *); void *frame_ctx; } ck_classic_read;
static void ck_classic_read_original(void *ctx, const char *game_path, const char *level_path) {
  ck_classic_read *read = ctx;
  if (read->loading)
    qa_app_sim_classic_guest_world_restore_original_loading(read->sim->source.classic_world, game_path, level_path, read->map->view,
        ck_restore_server_state, &read->server->restore, read->next_frame, read->frame_ctx);
  else qa_app_sim_classic_guest_world_restore_original(read->sim->source.classic_world, game_path, level_path, read->map->view,
      ck_restore_server_state, &read->server->restore);
}
/* nativeRestorePrelude plus both native original arms of restoreNativeLoading (loading) and restore (!nativeRestored). */
static void ck_restore_native_original(qa_app_shared_simulation *sim, const qa_save_image *save, bool loading,
    void (*next_frame)(void *frame_ctx), void *frame_ctx) {
  const char *map = sim->recipe.map.geometry.requested_path;
  if (qa_ss_is(sim->source.edition, "rerelease")) {
    qa_app_sim_q2_rerelease_native_save *original = ck_rerelease_original(sim, save);
    ck_require_native_original(sim, original, "Native rerelease save requires a matching non-deathmatch source");
    for (size_t i = 0; i < original->visited_level_count; i++) {
      const qa_app_sim_q2_rerelease_visited_level *level = &original->visited_levels[i];
      qa_ss_map_set(&sim->source.visited, &level->map, &level);
    }
    ck_native_map native = ck_native_map_of(sim); /* nativeRestorePrelude's request, built before init */
    if (loading) qa_app_sim_rerelease_guest_world_init_loading(sim->source.rerelease_world, next_frame, frame_ctx);
    else qa_app_sim_rerelease_guest_world_init(sim->source.rerelease_world);
    ck_require_non_deathmatch(sim);
    ck_server_storage server; ck_rerelease_server_storage(&server, sim, original);
    qa_app_sim_rerelease_guest_save saved = {original->game, original->level};
    if (loading) qa_app_sim_rerelease_guest_world_read_save_loading(sim->source.rerelease_world, saved, native.view, ck_restore_server_state,
        &server.restore, next_frame, frame_ctx);
    else qa_app_sim_rerelease_guest_world_read_save(sim->source.rerelease_world, saved, native.view, ck_restore_server_state, &server.restore);
    ck_server_storage_free(&server);
    ck_native_map_free(&native);
    return;
  }
  qa_q2_classic_original_save *original = ck_classic_original(sim, save);
  ck_require_native_original(sim, original, "Native original save requires a matching non-deathmatch source");
  for (size_t i = 0; i < original->visited_level_count; i++) {
    const qa_q2_classic_visited_level *level = &original->visited_levels[i];
    qa_ss_map_set(&sim->source.visited, &level->map, &level);
  }
  ck_native_map native = ck_native_map_of(sim); /* nativeRestorePrelude's request, built before init */
  if (loading) qa_app_sim_classic_guest_world_init_loading(sim->source.classic_world, next_frame, frame_ctx);
  else qa_app_sim_classic_guest_world_init(sim->source.classic_world);
  ck_require_non_deathmatch(sim);
  ck_server_storage server; ck_classic_server_storage(&server, sim, original);
  ck_classic_read read = {sim, &native, &server, loading, next_frame, frame_ctx};
  qa_q2_classic_original_identity identity = {qa_app_sim_classic_guest_world_module(sim->source.classic_world), map};
  if (loading) qa_classic_original_save_files_restore_loading(sim->source.files, original, identity, ck_classic_read_original, &read);
  else qa_classic_original_save_files_restore(sim->source.files, original, identity, ck_classic_read_original, &read);
  ck_server_storage_free(&server);
  ck_native_map_free(&native);
}
/* Q2NativeStrategy.restoreLoading(save, nextFrame). */
void qa_ss_q2_native_restore_loading(qa_app_shared_simulation *sim, const qa_save_image *save, qa_app_shared_sim_next_frame_fn next_frame,
    void *frame_ctx) {
  ck_restore_native_original(sim, save, true, next_frame, frame_ctx);
}

/* isDeepStrictEqual over InventoryEntry records. */
static bool ck_text_equal(const char *a, const char *b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }
static bool ck_entry_equal(const qa_inventory_entry *a, const qa_inventory_entry *b) {
  if (!ck_text_equal(a->item, b->item) || a->count != b->count || a->capacity != b->capacity || a->has_count_policy != b->has_count_policy) return false;
  return !a->has_count_policy || (ck_text_equal(a->count_policy.kind, b->count_policy.kind) && ck_text_equal(a->count_policy.arithmetic, b->count_policy.arithmetic));
}
static bool ck_entries_equal(const qa_inventory_entry *a, size_t a_count, const qa_inventory_entry *b, size_t b_count) {
  if (a_count != b_count) return false;
  for (size_t i = 0; i < a_count; i++) if (!ck_entry_equal(&a[i], &b[i])) return false;
  return true;
}
/* The `inventories` list restore narrows: borrowed rows or kept copies. */
typedef struct ck_inventories { qa_inventory_checkpoint *items; size_t count; } ck_inventories;
static bool ck_resolves_to(qa_app_shared_simulation *sim, qa_saved_actor_id saved, qa_owned_actor actor) {
  qa_owned_actor resolved;
  return qa_session_actor_registry_resolve_saved(sim->actors, saved, &resolved) && ck_same_owned(resolved, actor);
}
/* restoreInventoryCoverage(actors, legacy). */
static void ck_inventory_coverage(qa_app_shared_simulation *sim, const qa_save_image *save, ck_inventories *inventories,
    const qa_owned_actor *actors, size_t actor_count, bool legacy) {
  for (size_t a = 0; a < actor_count; a++) {
    qa_owned_actor actor = actors[a];
    size_t index = inventories->count;
    for (size_t i = 0; i < inventories->count && index == inventories->count; i++)
      if (ck_resolves_to(sim, inventories->items[i].actor, actor)) index = i;
    const qa_inventory_checkpoint *previous = index < inventories->count ? &inventories->items[index] : NULL;
    size_t entry_count = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor.id, &entry_count);
    if (!legacy) {
      if (previous == NULL || !ck_entries_equal(previous->entries, previous->entry_count, entries, entry_count))
        ck_fail("Saved native inventory disagrees with restored source storage");
      continue;
    }
    if (previous != NULL) for (size_t i = 0; i < previous->entry_count; i++) {
      const qa_inventory_entry *current = NULL;
      for (size_t k = 0; k < entry_count && current == NULL; k++) if (ck_text_equal(entries[k].item, previous->entries[i].item)) current = &entries[k];
      if (current == NULL || !ck_entry_equal(&previous->entries[i], current)) ck_fail("Legacy source inventory disagrees with restored source storage");
    }
    qa_saved_actor_id saved_actor; bool found = previous != NULL;
    if (found) saved_actor = previous->actor;
    else for (size_t i = 0; i < save->actor_count && !found; i++)
      if (ck_resolves_to(sim, save->actors[i].id, actor)) { saved_actor = save->actors[i].id; found = true; }
    if (!found) ck_fail("Restored source inventory has no saved actor identity");
    qa_inventory_entry *kept = qa_app_shared_sim_keep(sim, (entry_count ? entry_count : 1) * sizeof *kept);
    if (entry_count != 0) memcpy(kept, entries, entry_count * sizeof *kept);
    qa_inventory_checkpoint migrated = {saved_actor, kept, entry_count};
    if (index < inventories->count) inventories->items[index] = migrated;
    else {
      qa_inventory_checkpoint *grown = qa_app_shared_sim_keep(sim, (inventories->count + 1) * sizeof *grown);
      if (inventories->count != 0) memcpy(grown, inventories->items, inventories->count * sizeof *grown);
      grown[inventories->count] = migrated;
      inventories->items = grown; inventories->count++;
    }
  }
}

/* Restore-time closures over the simulation. */
typedef struct ck_restore_ctx {
  qa_app_shared_simulation *sim;
  qa_ss_map monsters; /* restoredMonsterActors: ActorId set */
  const qa_save_reader *ammo_timers; /* selectedAmmoTimers while its restore runs */
  qa_save_reader selected_source; bool hydrate_q1;
  ck_hosts hosts; qa_shared_world_restore_host host; qa_save_image shared;
} ck_restore_ctx;
static void ck_dispose_restore_ctx(void *ctx) {
  ck_restore_ctx *restore = ctx;
  qa_ss_map_free(&restore->monsters);
  ck_hosts_free(&restore->hosts);
  free(restore);
}
/* reconstructed(actor). */
static bool ck_reconstructed(void *ctx, qa_actor_id actor) {
  ck_restore_ctx *restore = ctx;
  return !qa_ss_map_has(&restore->monsters, &actor) && restore->sim->strategy->reconstructs_actor(restore->sim, actor);
}
static qa_world_storage_kind ck_world_storage(void *ctx, qa_owned_actor actor) {
  ck_restore_ctx *restore = ctx;
  qa_app_shared_simulation *sim = restore->sim;
  if (qa_ss_map_has(&restore->monsters, &actor.id)) return QA_WORLD_STORAGE_COPIED;
  if (ck_reconstructed(ctx, actor.id)) return QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED;
  if (sim->strategy->owns_supply(sim)) {
    qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, actor.id);
    if (source.has_value && qa_ss_is(source.provider, sim->recipe.map.entities.provider)) return QA_WORLD_STORAGE_PREBOUND;
  }
  return QA_WORLD_STORAGE_COPIED;
}
static qa_world_storage_kind ck_link_storage(void *ctx, qa_owned_actor actor) {
  return ck_reconstructed(ctx, actor.id) ? QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED : QA_WORLD_STORAGE_COPIED;
}
/* hydrateSelectedQ1: restore the selected Q1 weapon foundation before shared inventory sources hydrate. */
static void ck_selected_q1_restore(qa_app_shared_simulation *sim, const qa_save_reader *selected_source) {
  if (qa_app_sim_provider_family(sim->recipe.character.definition.provider) == QA_GAME_FAMILY_Q1) qa_app_shared_sim_q1_character_source(sim);
  qa_save_reader entities = ck_f(selected_source, "entities");
  qa_persist_q1_foundation_checkpoint saved = qa_read_q1_foundation_checkpoint(&entities);
  qa_save_reader_destroy(&entities);
  ck_q1_restore(sim->selected_weapon_source.q1_game, &saved, false);
  qa_q1_foundation_checkpoint_free(&saved);
}
static void ck_hydrate_selected_q1(void *ctx) {
  ck_restore_ctx *restore = ctx;
  qa_app_shared_simulation *sim = restore->sim;
  qa_save_reader kind = ck_f(&restore->selected_source, "kind");
  qa_save_reader_literal_string(&kind, "q1");
  qa_save_reader_destroy(&kind);
  ck_selected_q1_restore(sim, &restore->selected_source);
  qa_q1_services *game = sim->selected_weapon_source.q1_game;
  for (size_t i = 0; i < game->player_count; i++) {
    qa_q1_player_state *player = game->players[i];
    sim->strategy->set_max_health(sim, player->actor.id, player->max_health);
  }
}
static const char *ck_movement_kind(qa_movement_state_kind kind) {
  switch (kind) {
    case QA_MOVEMENT_STATE_Q1: return "q1-netquake";
    case QA_MOVEMENT_STATE_QW: return "q1-quakeworld";
    case QA_MOVEMENT_STATE_Q2: return "q2";
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return "q2-rerelease";
    case QA_MOVEMENT_STATE_Q3: return "q3";
  }
  return "";
}
static bool ck_mod_client(void *ctx, qa_actor_id actor, qa_client_id *out) { return qa_app_shared_sim_player_client(ctx, actor, out); }
static double ck_legacy_ammo_time(void *ctx, qa_actor_id actor, double weapon) {
  ck_restore_ctx *restore = ctx;
  double timer;
  if (!restore->sim->strategy->ammo_timer(restore->sim, actor, weapon, &timer))
    qa_save_reader_fail(restore->ammo_timers, "Saved ammo timer has no original source client");
  return timer;
}
static void ck_release_detached(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)frame; /* TS think: self => this.actors.release(self) */
  qa_session_actor_registry_release(((qa_app_shared_simulation *)ctx)->actors, self);
}
/* The restored client of a saved slot: options.restoredClients.find(slot). */
static bool ck_restored_client(qa_app_shared_simulation *sim, double slot, qa_client_id *out) {
  for (size_t i = 0; i < sim->options.restored_client_count; i++)
    if ((double)sim->options.restored_clients[i].slot == slot) { *out = sim->options.restored_clients[i]; return true; }
  return false;
}
static void ck_dispose_decoded_owner(void *ctx) { qa_persist_decoded_free(ctx); free(ctx); }
static void ck_dispose_mod_commands(void *ctx) { qa_app_sim_mod_client_commands_free(ctx); free(ctx); }
/* The receipts the commands map holds point into the read arrays. */
static qa_app_sim_mod_client_commands *ck_keep_mod_commands(qa_app_sim_mod_client_commands read) {
  qa_app_sim_mod_client_commands *kept = malloc(sizeof *kept);
  ck_oom(kept);
  *kept = read;
  return kept;
}
static void ck_dispose_owned_profile(void *ctx) { qa_composition_owned_profile_destroy(ctx); free(ctx); }
static void ck_dispose_pending_grapple(void *ctx) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_grapple_runtime_checkpoint_destroy(&sim->pending_qvm_grapple_restore_grapple);
  free(sim->pending_qvm_grapple_restore_slots);
  sim->pending_qvm_grapple_restore_slots = NULL; sim->pending_qvm_grapple_restore_slot_count = 0;
}
/* readSavedActor-based owner for readLegacyQ3Source. */
static qa_owned_actor ck_legacy_owner(void *ctx, const qa_save_reader *reader) { return ck_owner(ctx, reader); }
/* A read WeaponSlotRestoreState with its identities kept past the reader's tree. */
static qa_app_sim_weapon_slot_restore_state ck_intern_slot_state(qa_app_shared_simulation *sim, qa_app_sim_weapon_slot_restore_state state) {
  if (state.kind != NULL) state.kind = qa_app_shared_sim_intern(sim, state.kind);
  if (state.provider != NULL) state.provider = qa_app_shared_sim_intern(sim, state.provider);
  if (state.from != NULL) state.from = qa_app_shared_sim_intern(sim, state.from);
  if (state.next.provider != NULL) state.next.provider = qa_app_shared_sim_intern(sim, state.next.provider);
  if (state.next.item != NULL) state.next.item = qa_app_shared_sim_intern(sim, state.next.item);
  return state;
}

/* Q2NativeStrategy.restoreGuest(save, nativeRestored). */
void qa_ss_q2_native_restore_guest(qa_app_shared_simulation *sim, const qa_save_image *save, bool native_restored) {
  if (!native_restored) ck_restore_native_original(sim, save, false, NULL, NULL);
  size_t saved_count = 0; qa_save_value *saved_tree = NULL;
  qa_app_sim_native_saved_client *saved_clients = qa_app_sim_native_q2_saved_clients(*save, &saved_count, &saved_tree);
  for (size_t i = 0; i < saved_count; i++) {
    qa_client_id client;
    if (!ck_restored_client(sim, saved_clients[i].client_slot, &client)) ck_fail("Native saved client has no retained session identity");
    double slot = (double)client.slot + 1;
    qa_app_sim_classic_guest_connect connected = sim->source.classic_world != NULL
        ? qa_app_sim_classic_guest_world_connect(sim->source.classic_world, slot, saved_clients[i].userinfo)
        : qa_app_sim_rerelease_guest_world_connect(sim->source.rerelease_world, slot, saved_clients[i].userinfo, "", false);
    if (!connected.allowed) ck_fail("Native source rejected restored client");
    qa_app_shared_sim_register_q2_native_client_mode(sim, client, false);
    if (qa_ss_is(saved_clients[i].phase, "active")) {
      if (sim->source.classic_world != NULL) qa_app_sim_classic_guest_world_begin(sim->source.classic_world, slot);
      else qa_app_sim_rerelease_guest_world_begin(sim->source.rerelease_world, slot);
    }
  }
  free(saved_clients); qa_save_value_free(saved_tree);
  qa_module_identity module = sim->source.classic_world != NULL ? qa_app_sim_classic_guest_world_module(sim->source.classic_world)
      : qa_app_sim_rerelease_guest_world_module(sim->source.rerelease_world);
  qa_session_actor_registry_rebind_restored_source(sim->actors, module.id);
  if (qa_app_shared_sim_native_has_source_inventory(sim)) for (size_t i = 0; i < sim->source.clients.count; i++) {
    qa_owned_actor actor = *(const qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i);
    const qa_inventory_checkpoint *saved = NULL;
    for (size_t k = 0; k < save->inventory_count && saved == NULL; k++) if (ck_resolves_to(sim, save->inventories[k].actor, actor)) saved = &save->inventories[k];
    qa_app_shared_sim_bind_equipment_inventory(sim, actor, saved ? saved->entries : NULL, saved ? saved->entry_count : 0);
  }
}
/* Q3Strategy.restoreContinuation(save). */
void qa_ss_q3_restore_continuation(qa_app_shared_simulation *sim, const qa_save_image *save) {
  qa_app_sim_native_q3_runtime native = qa_app_sim_native_q3_runtime_reader(*save);
  const char *product = qa_app_q3_source_runtime_options(sim->source.q3)->product;
  for (size_t i = 0; i < native.arsenal_count; i++) {
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(sim->actors, native.arsenals[i].actor, &actor);
    if (!resolved || !qa_ss_map_has(&sim->player_states, &actor) || qa_ss_map_has(&sim->q3_arsenals, &actor)
        || !qa_ss_is(native.arsenals[i].state.product, product)) ck_fail("Saved Q3 arsenal has no unique matching player and product");
    qa_q3_arsenal_runtime_state state = native.arsenals[i].state;
    state.product = qa_app_shared_sim_intern(sim, state.product);
    qa_ss_map_set(&sim->q3_arsenals, &actor, &state);
  }
  qa_ss_map movement; qa_ss_map_init(&movement, QA_SS_KEY_OWNED, 0);
  for (size_t i = 0; i < native.movement_count; i++) {
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(sim->actors, native.movement[i].actor, &actor);
    qa_app_sim_movement_player **player = resolved ? qa_ss_map_get(&sim->player_states, &actor) : NULL;
    if (player == NULL || qa_ss_map_has(&movement, &actor)) { qa_ss_map_free(&movement); ck_fail("Saved Q3 movement has no unique matching player"); }
    qa_ss_map_set(&movement, &actor, NULL);
    const qa_app_sim_q3_runtime_movement *saved = &native.movement[i];
    (*player)->has_source_movement = saved->has_state;
    if (saved->has_state) {
      qa_app_sim_client_movement_options options; memset(&options, 0, sizeof options);
      options.has_trace_mask = true; options.trace_mask = saved->trace_mask;
      options.has_fixed_msec = saved->has_fixed_msec; options.fixed_msec = saved->fixed_msec;
      options.no_footsteps = saved->no_footsteps; options.gauntlet_hit = saved->gauntlet_hit; options.debug_level = saved->debug_level;
      options.order = QA_APP_SIM_MOVEMENT_ORDER_SAVED;
      (*player)->source_movement = options;
    }
  }
  size_t moved = movement.count;
  qa_ss_map_free(&movement);
  if (moved != sim->player_states.count || sim->q3_arsenals.count != sim->player_states.count) ck_fail("Saved Q3 player continuation is incomplete");
  for (size_t i = 0; i < native.last_attack_count; i++) {
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(sim->actors, native.last_attacks[i].actor, &actor) || qa_ss_map_has(&sim->last_attack, &actor))
      ck_fail("Saved Q3 attack has no unique matching actor");
    qa_actor_id *scratch = qa_app_shared_sim_keep(sim, 2 * sizeof *scratch);
    qa_attack_provenance attack = qa_q2_restore_attack(native.last_attacks[i].attack, ck_reference_saved, sim, &scratch[0], &scratch[1]);
    qa_ss_map_set(&sim->last_attack, &actor, &attack);
  }
  qa_app_sim_native_q3_runtime_free(&native);
  if (qa_app_sim_provider_family(sim->recipe.combat.provider) == QA_GAME_FAMILY_Q3) {
    qa_combat_policy bridge_policy = qa_q3_base_combat_bridge_policy(qa_app_q3_source_runtime_bridge(sim->source.q3));
    qa_app_shared_sim_keep_owned(sim, bridge_policy.ctx, ss_release_q3_bridge_policy);
    sim->dispose_source_combat = qa_gameplay_authority_register(sim->combat, bridge_policy);
    sim->has_dispose_source_combat = true;
  }
  qa_app_q3_source_runtime_finish_native_restore(sim->source.q3);
}
/* Q1Strategy.restoreFoundation(save, reader). */
void qa_ss_q1_restore_foundation(qa_app_shared_simulation *sim, const qa_save_image *save, const qa_save_reader *reader) {
  qa_save_value *source_cvars = qa_app_sim_saved_source_cvars(*save);
  if (source_cvars == NULL) {
    qa_save_reader list = ck_f(reader, "sourceCvars");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i);
      const char *name = ck_string_of(&value, "name"), *text = ck_string_of(&value, "value");
      qa_cvar_read snapshot;
      if (qa_cvar_registry_set(sim->source.q1_cvars, name, text, true, &snapshot)) qa_cvar_read_free(&snapshot);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
  }
  qa_save_value_free(source_cvars);
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(*save, "q1:foundation");
  qa_persist_q1_foundation_checkpoint saved;
  if (!qa_decode_q1_foundation_checkpoint(record.bytes.bytes, record.bytes.length, &saved)) qa_range_error("Out of memory");
  ck_q1_restore(sim->source.q1_game, &saved, false);
  qa_q1_foundation_checkpoint_free(&saved);
}
/* Q2Strategy.restoreFoundation(save). */
void qa_ss_q2_restore_foundation(qa_app_shared_simulation *sim, const qa_save_image *save, const qa_save_reader *reader) {
  (void)reader; /* TS Q2Strategy.restoreFoundation(save) takes no reader */
  qa_restore_q2_product(sim->source.product, save->providers, save->provider_count);
  qa_save_value *saved_cvars = qa_app_sim_saved_source_cvars(*save);
  if (sim->q2_server_registry != NULL && saved_cvars != NULL) qa_restore_q2_server_cvars(sim->q2_server_registry, saved_cvars);
  qa_save_value_free(saved_cvars);
}

void qa_app_shared_sim_restore(qa_app_shared_simulation *sim, const qa_save_image *save, bool native_restored) {
  if (sim->source_items_restore != NULL) save = &sim->source_items_restore->primary;
  sim->strategy->require_loaded(sim, "The selected source world does not expose a complete saved-game restore");
  sim->strategy->restore_guest(sim, save, native_restored);
  /* The deferred completion keeps the host, its storage closure and the
   * shared save until finish(): they live as long as the simulation. */
  ck_restore_ctx *restore = calloc(1, sizeof *restore);
  ck_oom(restore);
  restore->sim = sim;
  qa_ss_map_init(&restore->monsters, QA_SS_KEY_ACTOR, 0);
  ck_own(sim, ck_dispose_restore_ctx, restore);
  qa_save_value *tree = NULL;
  qa_save_reader reader = qa_app_sim_simulation_save_reader(*save, &tree);
  bool keep_tree = false;
  /* savedSelectedMonsters and restoredMonsterActors. */
  qa_app_sim_selected_monsters_checkpoint saved_monsters; bool has_saved_monsters = sim->selected_monsters != NULL;
  qa_save_reader monsters = ck_f(&reader, "selectedMonsters");
  if (has_saved_monsters) {
    saved_monsters = qa_app_sim_read_selected_monsters_checkpoint(&monsters);
    if (sim->strategy->uses_guest_monsters(sim))
      for (size_t i = 0; i < saved_monsters.authored_count; i++) {
        qa_owned_actor actor;
        if (qa_session_actor_registry_resolve_saved(sim->actors, saved_monsters.authored[i].actor, &actor)) qa_ss_map_set(&restore->monsters, &actor.id, NULL);
      }
  }
  {
    qa_save_reader list = ck_f(&reader, "nativeEquipmentVelocity");
    if (ck_present(&list)) for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader entry = ck_i(&list, i), actor_r = ck_f(&entry, "actor");
      qa_actor_id actor = ck_reference(sim, &actor_r);
      qa_vec3 velocity = ck_vector_of(&entry, "velocity");
      qa_ss_map_set(&sim->native_equipment_velocity, &actor, &velocity);
      qa_save_reader_destroy(&actor_r); qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "nativeWeaponRequests");
    if (ck_present(&list)) for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader entry = ck_i(&list, i), actor_r = ck_f(&entry, "actor");
      qa_actor_id actor = ck_reference(sim, &actor_r);
      double weapon = ck_integer_of(&entry, "weapon", 0);
      qa_ss_map_set(&sim->native_weapon_requests, &actor, &weapon);
      qa_save_reader_destroy(&actor_r); qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
  }
  {
    const qa_random_state *random = NULL;
    for (size_t i = 0; i < save->random_count && random == NULL; i++)
      if (qa_ss_is(save->random[i].provider, sim->recipe.map.entities.provider)) random = &save->random[i].state;
    if (random == NULL || (random->kind != QA_RANDOM_STATE_GLIBC_RANDOM && random->kind != QA_RANDOM_STATE_Q2_RERELEASE_MT19937))
      ck_fail("Save has no matching source random stream");
    ck_random_restore(sim->random, random);
  }
  sim->host_milliseconds = ck_finite_of(&reader, "hostMilliseconds");
  {
    qa_save_reader paused = ck_f(&reader, "q1Paused");
    sim->q1_pause_state = ck_present(&paused) ? qa_save_reader_boolean(&paused) : false;
    qa_save_reader_destroy(&paused);
    qa_save_reader scheduling = ck_f(&reader, "sourceSchedulingMilliseconds");
    sim->source_scheduling_milliseconds = ck_present(&scheduling) ? qa_save_reader_finite(&scheduling) : sim->host_milliseconds;
    qa_save_reader_destroy(&scheduling);
  }
  sim->attack_sequence = ck_integer_of(&reader, "attackSequence", 0);
  {
    qa_save_reader visibility = ck_f(&reader, "q1ClientVisibility");
    qa_q1_client_visibility_restore(sim->q1_client_visibility, visibility.value);
    qa_save_reader_destroy(&visibility);
    qa_save_reader campaign = ck_f(&reader, "campaign");
    sim->q1_campaign.flags = ck_number_of(&campaign, "flags");
    qa_save_reader skill = ck_f(&campaign, "skill");
    static const double skills[] = {0, 1, 2, 3};
    sim->q1_campaign.skill = (int)qa_save_reader_choice_number(&skill, skills, 4);
    qa_save_reader_destroy(&skill); qa_save_reader_destroy(&campaign);
  }
  if (sim->strategy->owns_equipment_players(sim)
      && (sim->recipe.equipment.grapple.is_enabled || sim->recipe.equipment.hand_grenades.is_enabled)) {
    for (size_t i = 0; i < save->inventory_count; i++) {
      qa_owned_actor actor; qa_client_id client;
      if (qa_session_actor_registry_resolve_saved(sim->actors, save->inventories[i].actor, &actor) && qa_app_shared_sim_player_client(sim, actor.id, &client)
          && !qa_shared_inventory_table_has(sim->inventory, actor.id))
        qa_app_shared_sim_bind_equipment_inventory(sim, actor, save->inventories[i].entries, save->inventories[i].entry_count);
    }
  }
  {
    qa_save_reader declaration = ck_f(&reader, "qvmPrimaryDeclaration");
    const char *expected = sim->strategy->expected_declaration(sim);
    const char *saved = !ck_present(&declaration) || qa_save_reader_is_null(&declaration) ? NULL : qa_save_reader_string(&declaration);
    if (!ck_text_equal(saved, expected)) qa_save_reader_fail(&declaration, "saved primary QVM interfaces differ from the mounted compatibility declaration");
    qa_save_reader_destroy(&declaration);
  }
  qa_save_reader armor_projection = ck_f(&reader, "qvmArmorProjection");
  if (ck_present(&armor_projection)) qa_save_reader_literal_number(&armor_projection, 1);
  qa_qvm_combat_bindings *legacy_qvm_combat = !ck_present(&armor_projection) ? sim->strategy->legacy_combat(sim) : NULL;
  qa_save_reader_destroy(&armor_projection);
  qa_save_reader inventory_projection = ck_f(&reader, "qvmInventoryProjection");
  bool has_qvm_inventory_projection = ck_present(&inventory_projection);
  if (has_qvm_inventory_projection) {
    qa_save_reader_literal_number(&inventory_projection, 1);
    sim->strategy->require_inventory_projection(sim, "qvm");
  }
  qa_save_reader_destroy(&inventory_projection);
  qa_save_reader native_projection = ck_f(&reader, "nativeInventoryProjection");
  bool has_native_projection = ck_present(&native_projection);
  if (has_native_projection) {
    qa_save_reader_literal_number(&native_projection, 1);
    sim->strategy->require_inventory_projection(sim, "native");
  }
  qa_save_reader_destroy(&native_projection);
  ck_inventories inventories = {(qa_inventory_checkpoint *)save->inventories, save->inventory_count};
  if (save->inventory_count != 0) {
    qa_inventory_checkpoint *copy = qa_app_shared_sim_keep(sim, save->inventory_count * sizeof *copy);
    memcpy(copy, save->inventories, save->inventory_count * sizeof *copy);
    inventories.items = copy;
  }
  {
    qa_app_source_inventory_coverage coverage;
    if (sim->strategy->inventory_coverage(sim, has_native_projection, has_qvm_inventory_projection, &coverage))
      ck_inventory_coverage(sim, save, &inventories, coverage.actors, coverage.actor_count, coverage.legacy);
  }
  restore->selected_source = ck_f(&reader, "selectedWeaponSource");
  qa_ss_selected_weapon_source *weapon_source = sim->has_selected_weapon_source ? &sim->selected_weapon_source : NULL;
  restore->hydrate_q1 = sim->strategy->owns_supply(sim) && weapon_source != NULL && qa_ss_is(weapon_source->kind, "q1");
  {
    /* sharedSave: effective inventories and legacy QVM armor. */
    qa_save_image shared = *save;
    if (sim->source_items_restore != NULL) {
      const qa_inventory_checkpoint *effective = qa_source_item_restore_effective(sim->source_items_restore, inventories.items, inventories.count,
          &shared.inventory_count);
      ck_oom(effective);
      shared.inventories = effective;
    } else { shared.inventories = inventories.items; shared.inventory_count = inventories.count; }
    if (legacy_qvm_combat != NULL) {
      qa_combat_checkpoint *combat = qa_app_shared_sim_keep(sim, (save->combat_count ? save->combat_count : 1) * sizeof *combat);
      for (size_t i = 0; i < save->combat_count; i++) {
        combat[i] = save->combat[i];
        qa_owned_actor actor;
        if (!qa_session_actor_registry_resolve_saved(sim->actors, save->combat[i].actor, &actor)) continue;
        qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, actor.id);
        if (!source.has_value || !qa_ss_is(source.provider, sim->recipe.map.entities.provider)) continue;
        combat[i].state.armor = qa_qvm_combat_bindings_normalize_legacy_armor(legacy_qvm_combat, actor.id, save->combat[i].state.armor);
      }
      shared.combat = combat;
    }
    ck_hosts_init(&restore->hosts, sim);
    restore->shared = shared;
    restore->host = (qa_shared_world_restore_host){restore->hosts.actors, restore->hosts.bodies, restore->hosts.combat, restore->hosts.inventory,
        restore, ck_world_storage};
    sim->pending_shared_restore = qa_restore_shared_world_state_deferred(&restore->shared, &restore->host,
        restore->hydrate_q1 ? ck_hydrate_selected_q1 : NULL, restore);
    ck_oom(sim->pending_shared_restore);
  }
  if (restore->hydrate_q1) keep_tree = true;
  {
    qa_save_reader players = ck_f(&reader, "players");
    for (size_t i = 0, n = qa_save_reader_list_count(&players); i < n; i++) {
      qa_save_reader value = ck_i(&players, i);
      qa_app_sim_saved_movement_player saved = qa_app_sim_read_movement_player(&value, ck_reference_saved, sim);
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_client_id client;
      if (!ck_restored_client(sim, saved.client_slot, &client) || !qa_app_shared_sim_owns_client(sim, client))
        qa_save_reader_fail(&value, "Saved player has no connected session client");
      qa_body_state_result body = qa_shared_body_table_read((void *)qa_app_sim_shared_physics_bodies(sim->physics), actor.id);
      if (!body.has_value) qa_save_reader_fail(&value, "Saved player has no body");
      /* The saved arsenal's ammo outlives the reader's tree. */
      qa_inventory_entry *ammo = qa_app_shared_sim_keep(sim, (saved.arsenal.ammo_count ? saved.arsenal.ammo_count : 1) * sizeof *ammo);
      for (size_t k = 0; k < saved.arsenal.ammo_count; k++) {
        ammo[k] = saved.arsenal.ammo[k];
        ammo[k].item = qa_app_shared_sim_intern(sim, ammo[k].item);
      }
      saved.arsenal.ammo = ammo;
      saved.arsenal.provider = qa_app_shared_sim_intern(sim, saved.arsenal.provider);
      if (saved.arsenal.active_weapon != NULL) saved.arsenal.active_weapon = qa_app_shared_sim_intern(sim, saved.arsenal.active_weapon);
      if (saved.arsenal.state.pending_weapon != NULL) saved.arsenal.state.pending_weapon = qa_app_shared_sim_intern(sim, saved.arsenal.state.pending_weapon);
      qa_provider_reference definition, appearance;
      if (saved.has_character) {
        definition.provider = qa_app_shared_sim_intern(sim, saved.character_definition.provider);
        definition.content = qa_app_shared_sim_intern(sim, saved.character_definition.content);
      }
      if (saved.has_character_appearance) {
        appearance.provider = qa_app_shared_sim_intern(sim, saved.character_appearance.provider);
        appearance.content = qa_app_shared_sim_intern(sim, saved.character_appearance.content);
      }
      qa_app_sim_movement_player *player = qa_app_shared_sim_create_player_with(sim, actor, client, body.value.origin, saved.view_angles, saved.arsenal,
          saved.has_character ? &definition : NULL, saved.has_character_appearance ? &appearance : NULL);
      if (!qa_ss_is(ck_movement_kind(saved.state.kind), player->profile.kind))
        qa_save_reader_fail(&value, "Saved movement state differs from the selected movement provider");
      /* Object.assign(player, state) over every saved member but actor,
       * clientSlot, characterDefinition and characterAppearance. */
      player->has_body_shape_base = saved.has_body_shape_base; player->body_shape_base = saved.body_shape_base;
      player->fixed_pose_active = saved.fixed_pose_active;
      player->has_arsenal_intent = saved.has_arsenal_intent; player->arsenal_intent = saved.arsenal_intent;
      if (saved.has_arsenal_intent) {
        player->arsenal_intent.provider = qa_app_shared_sim_intern(sim, saved.arsenal_intent.provider);
        if (saved.arsenal_intent.weapon != NULL) player->arsenal_intent.weapon = qa_app_shared_sim_intern(sim, saved.arsenal_intent.weapon);
      }
      player->has_netquake_command = saved.has_netquake_command; player->netquake_command = saved.netquake_command;
      player->flight = saved.flight;
      player->state = saved.state; player->arsenal = saved.arsenal; player->animation = saved.animation;
      player->animation.provider = qa_app_shared_sim_intern(sim, saved.animation.provider);
      player->view_angles = saved.view_angles; player->command_angles = saved.command_angles; player->view_height = saved.view_height;
      player->bounds = saved.bounds; player->ground = saved.ground;
      player->water_level = saved.water_level; player->water_type = saved.water_type; player->intermission = saved.intermission;
      player->gravity_multiplier = saved.gravity_multiplier; player->world_gravity = saved.world_gravity;
      player->buttons = saved.buttons; player->previous_buttons = saved.previous_buttons;
      player->last_sequence = saved.last_sequence; player->last_weapon_seconds = saved.last_weapon_seconds;
      player->has_cutscene = saved.has_cutscene; player->cutscene_origin = saved.cutscene_origin;
      player->cutscene_angles = saved.cutscene_angles; player->cutscene_view_offset = saved.cutscene_view_offset;
      qa_ss_map_set(&sim->player_states, &actor, &player);
      free(saved.ammo);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&players);
  }
  sim->strategy->restore_continuation(sim, save);
  qa_save_reader *selected_source = &restore->selected_source;
  if (weapon_source != NULL) {
    qa_save_reader source_kind = ck_f(selected_source, "kind");
    qa_save_reader_literal_string(&source_kind, weapon_source->kind);
    qa_save_reader_destroy(&source_kind);
    qa_save_reader random_r = ck_f(selected_source, "random");
    qa_random_state random = qa_read_random(&random_r);
    if (random.kind != QA_RANDOM_STATE_GLIBC_RANDOM && random.kind != QA_RANDOM_STATE_Q2_RERELEASE_MT19937) {
      free((void *)random.words.values);
      qa_save_reader_fail(&random_r, "Selected source requires its native random stream");
    }
    ck_random_restore(weapon_source->random, &random);
    free((void *)random.words.values);
    qa_save_reader_destroy(&random_r);
    if (qa_ss_is(weapon_source->kind, "q1")) {
      if (!restore->hydrate_q1) ck_selected_q1_restore(sim, selected_source);
    } else {
      qa_save_reader saved_frame = ck_f(selected_source, "frame"), time = ck_f(&saved_frame, "time"), elapsed = ck_f(&saved_frame, "elapsed");
      static const char *const time_kinds[] = {"seconds", "milliseconds"};
      qa_save_reader time_kind = ck_f(&time, "kind"), elapsed_kind = ck_f(&elapsed, "kind");
      qa_save_reader_literal_string(&time_kind, time_kinds[weapon_source->frame.time.kind == QA_SOURCE_TIME_MILLISECONDS]);
      qa_frame_context frame = weapon_source->frame;
      frame.frame = ck_integer_of(&saved_frame, "frame", 0);
      frame.time.value = ck_finite_of(&time, "value");
      qa_save_reader_literal_string(&elapsed_kind, time_kinds[weapon_source->frame.elapsed.kind == QA_SOURCE_TIME_MILLISECONDS]);
      frame.elapsed.value = ck_finite_of(&elapsed, "value");
      static const char *const phases[] = {"frame-entry", "client-command", "entity-think", "frame-exit"};
      qa_save_reader phase = ck_f(&saved_frame, "phase");
      const char *phase_name = qa_save_reader_choice_string(&phase, phases, 4);
      for (int p = 0; p < 4; p++) if (strcmp(phase_name, phases[p]) == 0) frame.phase = (qa_frame_phase)p;
      weapon_source->frame = frame;
      qa_save_reader_destroy(&phase); qa_save_reader_destroy(&time_kind); qa_save_reader_destroy(&elapsed_kind);
      qa_save_reader_destroy(&time); qa_save_reader_destroy(&elapsed); qa_save_reader_destroy(&saved_frame);
      qa_save_reader interval = ck_f(selected_source, "intervalMilliseconds");
      qa_save_reader_literal_number(&interval, weapon_source->interval_milliseconds);
      qa_save_reader_destroy(&interval);
      weapon_source->map_milliseconds = ck_finite_of(selected_source, "mapMilliseconds");
      weapon_source->next_milliseconds = ck_finite_of(selected_source, "nextMilliseconds");
      if (weapon_source->map_milliseconds < 0 || weapon_source->next_milliseconds <= weapon_source->map_milliseconds
          || weapon_source->next_milliseconds - weapon_source->map_milliseconds > weapon_source->interval_milliseconds)
        qa_save_reader_fail(selected_source, "Selected Q2 source deadline must follow admitted map progress");
      qa_persist_decoded *owner = calloc(1, sizeof *owner);
      ck_oom(owner);
      ck_own(sim, ck_dispose_decoded_owner, owner);
      qa_save_reader entities = ck_f(selected_source, "entities"), weapons = ck_f(selected_source, "weapons");
      qa_q2_foundation_checkpoint saved_entities = qa_read_q2_foundation_checkpoint(&entities, owner);
      qa_q2_entity_services_restore(qa_q2_entity_services_of_game(weapon_source->q2_game), saved_entities);
      qa_q2w_weapons_checkpoint saved_weapons = qa_read_q2_weapons_checkpoint(&weapons, owner);
      qa_q2w_weapons_restore(weapon_source->weapons, weapon_source->q2_game, &saved_weapons);
      qa_save_reader_destroy(&entities); qa_save_reader_destroy(&weapons);
      if (!sim->has_selected_arsenal || !qa_ss_is(sim->selected_arsenal.family, "q2")) qa_save_reader_fail(selected_source, "Selected Q2 source has no arsenal");
      qa_save_reader turns = ck_f(selected_source, "turns");
      for (size_t i = 0, n = qa_save_reader_list_count(&turns); i < n; i++) {
        qa_save_reader value = ck_i(&turns, i), state = ck_f(&value, "state"), firing = ck_f(&state, "firing");
        qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
        qa_app_q2_selected_weapon_turn_state turn; memset(&turn, 0, sizeof turn);
        turn.turn.buttons = ck_any_integer_of(&state, "buttons");
        turn.turn.latched_buttons = ck_any_integer_of(&state, "latchedButtons");
        turn.turn.weapon_thunk = ck_bool_of(&state, "weaponThunk");
        if (ck_given(&firing)) {
          turn.has_firing = true;
          turn.firing_weapon = qa_app_shared_sim_intern(sim, ck_string_of(&firing, "weapon"));
          turn.firing_credit = ck_number_of(&firing, "credit");
        }
        qa_app_q2_selected_arsenal_restore_turn(sim->selected_arsenal.q2, actor.id, turn);
        qa_save_reader_destroy(&firing); qa_save_reader_destroy(&state); qa_save_reader_destroy(&value);
      }
      qa_save_reader_destroy(&turns);
    }
    for (size_t i = 0; i < sim->player_states.count; i++) {
      qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
      player->arsenal = qa_app_shared_sim_arsenal(sim, player);
    }
  } else if (!qa_save_reader_is_null(selected_source)) qa_save_reader_fail(selected_source, "Saved selected weapon source has no matching authority");
  sim->strategy->restore_foundation(sim, save, &reader);
  {
    qa_save_reader times = ck_f(&reader, "nativeWeaponTimes");
    qa_ss_map_clear(&sim->native_weapon_times);
    if (ck_present(&times)) for (size_t i = 0, n = qa_save_reader_list_count(&times); i < n; i++) {
      qa_save_reader value = ck_i(&times, i);
      qa_actor_id actor = ck_owner_of(sim, &value, "actor").id;
      double time = ck_finite_of(&value, "time");
      if ((sim->strategy->as_q2_native(sim) == NULL) || !sim->has_selected_arsenal || qa_ss_map_has(&sim->native_weapon_times, &actor))
        qa_save_reader_fail(&value, "Saved native weapon clock has no unique selected owner");
      qa_ss_map_set(&sim->native_weapon_times, &actor, &time);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&times);
  }
  qa_save_reader selected_q3 = ck_f(&reader, "selectedQ3Source"), old_ballistics = ck_f(&reader, "selectedBallistics");
  bool legacy_q3 = ck_given(&old_ballistics);
  bool q3_arsenal = sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q3");
  qa_save_reader selected = ck_f(&reader, "selectedArsenals");
  size_t client_count = 0;
  qa_owned_actor *client_actors = NULL; qa_app_q3_selected_arsenal_checkpoint *client_states = NULL;
  if (q3_arsenal) {
    client_count = qa_save_reader_list_count(&selected);
    client_actors = qa_app_shared_sim_keep(sim, (client_count ? client_count : 1) * sizeof *client_actors);
    client_states = qa_app_shared_sim_keep(sim, (client_count ? client_count : 1) * sizeof *client_states);
    for (size_t i = 0; i < client_count; i++) {
      qa_save_reader value = ck_i(&selected, i), state = ck_f(&value, "state");
      client_actors[i] = ck_owner_of(sim, &value, "actor");
      client_states[i] = qa_app_read_q3_selected_arsenal_checkpoint(&state);
      qa_save_reader_destroy(&state); qa_save_reader_destroy(&value);
    }
    keep_tree = true; /* the read arsenal checkpoints borrow the tree */
  }
  if (sim->selected_q3_source != NULL) {
    if (legacy_q3) {
      if (ck_given(&selected_q3)) qa_save_reader_fail(&old_ballistics, "Saved Q3 source has two projectile owners");
      if (sim->strategy->legacy_q3_family(sim) == NULL) qa_save_reader_fail(&old_ballistics, "Legacy Q3 source requires its original foreign map");
      qa_app_legacy_q3_source saved = qa_app_read_legacy_q3_source(&old_ballistics, ck_legacy_owner, sim, ck_reference_saved, sim);
      sim->selected_milliseconds = saved.milliseconds; sim->selected_q3_time = trunc(saved.milliseconds);
      qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider);
      if (timing.clock.kind != QA_CLOCK_Q3 || sim->selected_q3_time < 0 || sim->selected_q3_time > 2147483647)
        qa_save_reader_fail(&old_ballistics, "Invalid legacy Q3 source clock");
      sim->selected_q3_next = sim->selected_q3_time + timing.clock.server_frame_milliseconds;
      bool covered = client_count == sim->player_states.count;
      for (size_t i = 0; i < client_count && covered; i++) {
        if (!qa_ss_map_has(&sim->player_states, &client_actors[i])) covered = false;
        for (size_t k = 0; k < i && covered; k++) if (ck_same_owned(client_actors[k], client_actors[i])) covered = false;
      }
      if (!covered) qa_save_reader_fail(&old_ballistics, "Legacy Q3 source client coverage differs from its saved players");
      qa_app_q3_selected_source_restore_legacy(sim->selected_q3_source, &saved, client_actors, client_states, client_count);
      qa_app_legacy_q3_source_destroy(&saved);
    } else {
      sim->selected_milliseconds = ck_finite_of(&selected_q3, "milliseconds");
      sim->selected_q3_time = ck_finite_of(&selected_q3, "time");
      sim->selected_q3_next = ck_finite_of(&selected_q3, "next");
      qa_save_reader strings = ck_f(&selected_q3, "strings");
      size_t n = qa_save_reader_list_count(&strings);
      double *indices = calloc(n ? n : 1, sizeof *indices); const char **texts = calloc(n ? n : 1, sizeof *texts);
      ck_oom(indices); ck_oom(texts);
      for (size_t i = 0; i < n; i++) {
        qa_save_reader value = ck_i(&strings, i);
        indices[i] = ck_integer_of(&value, "index", 0); texts[i] = ck_string_of(&value, "value");
        qa_save_reader_destroy(&value);
      }
      for (size_t i = 0; i < n; i++) {
        char **previous = qa_ss_map_get(&sim->selected_q3_strings, &indices[i]);
        char *copy = qa_ss_strdup(texts[i]);
        if (previous != NULL) { free(*previous); *previous = copy; }
        else qa_ss_map_set(&sim->selected_q3_strings, &indices[i], &copy);
      }
      free(indices); free(texts);
      qa_save_reader_destroy(&strings);
      qa_save_reader source_value = ck_f(&selected_q3, "source");
      qa_app_q3_selected_source_restore(sim->selected_q3_source, source_value.value);
      qa_save_reader_destroy(&source_value);
    }
  } else if (legacy_q3 || ck_given(&selected_q3)) qa_save_reader_fail(&selected_q3, "Saved Q3 source has no selected owner");
  if (q3_arsenal) {
    for (size_t i = 0; i < client_count; i++) {
      const char *family = sim->strategy->legacy_q3_family(sim);
      qa_app_q3_selected_arsenal_checkpoint state = client_states[i];
      if (legacy_q3 && family != NULL) {
        qa_composition_owned_profile *profile = calloc(1, sizeof *profile);
        ck_oom(profile);
        ck_own(sim, ck_dispose_owned_profile, profile);
        state = qa_app_migrate_legacy_q3_arsenal(state, family, profile);
      }
      qa_app_q3_selected_arsenal_restore(sim->selected_arsenal.q3, client_actors[i], state);
    }
    for (size_t i = 0; i < sim->player_states.count; i++) {
      qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
      player->arsenal = qa_app_shared_sim_arsenal(sim, player);
    }
  } else if (ck_given(&selected)) qa_save_reader_fail(&selected, "Saved selected arsenal has no matching authority");
  qa_save_reader_destroy(&selected); qa_save_reader_destroy(&selected_q3); qa_save_reader_destroy(&old_ballistics);
  {
    qa_ss_map_clear(&sim->restored_native_inventory_selections);
    qa_save_reader selections = ck_f(&reader, "nativeInventorySelections");
    if (ck_present(&selections)) for (size_t i = 0, n = qa_save_reader_list_count(&selections); i < n; i++) {
      qa_save_reader value = ck_i(&selections, i), item_r = ck_f(&value, "item");
      qa_actor_id actor = ck_owner_of(sim, &value, "actor").id;
      qa_ss_optional_item item = {!qa_save_reader_is_null(&item_r), NULL};
      if (item.has) item.item = qa_app_shared_sim_intern(sim, qa_namespaced(&item_r));
      if ((sim->strategy->as_q2_native(sim) == NULL) || !qa_shared_inventory_table_has(sim->inventory, actor) || qa_ss_map_has(&sim->restored_native_inventory_selections, &actor))
        qa_save_reader_fail(&value, "Saved native inventory cursor has no unique selected owner");
      qa_ss_map_set(&sim->restored_native_inventory_selections, &actor, &item);
      qa_save_reader_destroy(&item_r); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&selections);
  }
  if (has_saved_monsters) qa_app_shared_sim_restore_selected_monsters(sim, &saved_monsters);
  else if (!qa_save_reader_is_null(&monsters)) qa_save_reader_fail(&monsters, "Saved selected monsters have no matching admission");
  qa_save_reader_destroy(&monsters);
  {
    qa_save_reader foreign = ck_f(&reader, "foreignWorld");
    if (sim->has_foreign_world) {
      if (!ck_given(&foreign)) qa_save_reader_fail(&foreign, "Foreign world adjunct has no saved state");
      qa_app_sim_foreign_q2_world_checkpoint saved = qa_app_sim_read_foreign_q2_world_checkpoint(&foreign);
      qa_app_shared_sim_restore_foreign_world(sim, &saved);
      qa_app_sim_foreign_q2_world_checkpoint_free(&saved);
    } else if (ck_given(&foreign)) qa_save_reader_fail(&foreign, "Saved foreign world has no matching adjunct");
    qa_save_reader_destroy(&foreign);
    qa_save_reader foreign_q1 = ck_f(&reader, "foreignQ1World");
    if (sim->has_foreign_q1_world) {
      if (!ck_given(&foreign_q1)) qa_save_reader_fail(&foreign_q1, "Foreign Q1 world adjunct has no saved state");
      qa_app_sim_foreign_q1_world_checkpoint saved = qa_app_sim_read_foreign_q1_world_checkpoint(&foreign_q1);
      qa_app_shared_sim_restore_foreign_q1_world(sim, &saved);
      qa_app_sim_foreign_q1_world_checkpoint_free(&saved);
    } else if (ck_given(&foreign_q1)) qa_save_reader_fail(&foreign_q1, "Saved foreign Q1 world has no matching adjunct");
    qa_save_reader_destroy(&foreign_q1);
  }
  {
    qa_save_reader grapple = ck_f(&reader, "grapple"), slots = ck_f(&reader, "weaponSlots");
    if (sim->grapple != NULL) {
      qa_app_sim_grapple_runtime_checkpoint saved = qa_app_sim_read_grapple_runtime_checkpoint(&grapple);
      qa_app_sim_grapple_runtime_restore(sim->grapple, &saved);
      qa_app_sim_grapple_runtime_checkpoint_destroy(&saved);
    } else if (sim->recipe.equipment.grapple.is_enabled && qa_ss_is(sim->recipe.equipment.grapple.mechanic, "q3-qvm")) {
      sim->pending_qvm_grapple_restore_grapple = qa_app_sim_read_grapple_runtime_checkpoint(&grapple);
      sim->pending_qvm_grapple_restore_slots = qa_app_sim_read_weapon_slots(&slots, &sim->pending_qvm_grapple_restore_slot_count);
      sim->has_pending_qvm_grapple_restore = true;
      ck_own(sim, ck_dispose_pending_grapple, sim);
      keep_tree = true; /* the pending checkpoint borrows the tree until initializeQvmGrapple */
    } else if (ck_given(&grapple)) qa_save_reader_fail(&grapple, "Saved grapple has no selected controller");
    /* weaponSlots.clear(): the C map owns its slots. */
    for (size_t i = 0; i < sim->weapon_slots.count; i++) qa_app_sim_weapon_slot_destroy(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i));
    qa_ss_map_clear(&sim->weapon_slots);
    if (!sim->has_pending_qvm_grapple_restore) {
      size_t count = 0;
      qa_app_sim_saved_weapon_slot *saved = qa_app_sim_read_weapon_slots(&slots, &count);
      for (size_t i = 0; i < count; i++) {
        qa_owned_actor actor;
        if (!qa_session_actor_registry_resolve_saved(sim->actors, saved[i].actor, &actor)) { free(saved); ck_fail("Saved slot actor is missing"); }
        qa_app_sim_weapon_slot_restore_state state = ck_intern_slot_state(sim, saved[i].state);
        qa_app_shared_sim_bind_weapon_slot_state(sim, actor.id, &state);
      }
      free(saved);
    }
    qa_save_reader_destroy(&grapple); qa_save_reader_destroy(&slots);
    qa_save_reader equipment = ck_f(&reader, "handGrenades");
    if (sim->hand_grenades != NULL) {
      qa_app_sim_hand_grenade_runtime_checkpoint saved = qa_app_sim_read_hand_grenade_runtime_checkpoint(&equipment);
      qa_app_sim_hand_grenade_runtime_restore(sim->hand_grenades, &saved);
      qa_app_sim_hand_grenade_runtime_checkpoint_destroy(&saved);
    } else if (ck_given(&equipment)) qa_save_reader_fail(&equipment, "Saved equipment has no selected controller");
    qa_save_reader_destroy(&equipment);
    if (sim->strategy->has_guest_ui(sim) && (sim->grapple != NULL || sim->hand_grenades != NULL)) {
      size_t count = 0;
      const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
      qa_actor_id *actors = qa_app_shared_sim_keep(sim, (count ? count : 1) * sizeof *actors);
      if (count != 0) memcpy(actors, players, count * sizeof *actors);
      for (size_t i = 0; i < count; i++) {
        qa_ss_map_set(&sim->native_equipment_players, &actors[i], NULL);
        bool available = qa_app_shared_sim_equipment_player_available(sim, actors[i]);
        qa_ss_map_set(&sim->native_equipment_alive, &actors[i], &available);
      }
    }
  }
  {
    qa_save_reader foundation = ck_f(&reader, "q1CharacterFoundation");
    if (ck_given(&foundation)) {
      qa_persist_q1_foundation_checkpoint saved = qa_read_q1_foundation_checkpoint(&foundation);
      ck_q1_restore(qa_app_shared_sim_q1_character_source(sim), &saved, false);
      qa_q1_foundation_checkpoint_free(&saved);
    }
    qa_save_reader_destroy(&foundation);
    qa_save_reader list = ck_f(&reader, "q1Characters");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), bytes_r = ck_f(&value, "bytes");
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_q1_character_actor *character = qa_app_shared_sim_attach_q1_character(sim, qa_app_shared_sim_require_player(sim, actor.id));
      size_t length = 0;
      const uint8_t *bytes = qa_save_reader_bytes(&bytes_r, &length);
      qa_q1_character_actor_restore(character, bytes, length);
      qa_save_reader_destroy(&bytes_r); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "q2Characters");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), state = ck_f(&value, "state");
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_app_shared_sim_attach_q2_character(sim, qa_app_shared_sim_require_player(sim, actor.id));
      qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &actor);
      if (character == NULL) qa_save_reader_fail(&value, "Q2 character was not bound");
      qa_persist_decoded *owner = calloc(1, sizeof *owner);
      ck_oom(owner);
      ck_own(sim, ck_dispose_decoded_owner, owner);
      qa_q2_base_character_checkpoint checkpoint = qa_read_q2_character_checkpoint(&state, owner);
      qa_q2_character_actor_restore(*character, checkpoint, ck_reference_saved, sim);
      qa_save_reader_destroy(&state); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "characterStarts");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), start = ck_f(&value, "start");
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_ss_optional_actor saved; memset(&saved, 0, sizeof saved); saved.has = !qa_save_reader_is_null(&start);
      if (saved.has) saved.actor = ck_reference(sim, &start);
      qa_ss_map_set(&sim->character_starts, &actor, &saved);
      qa_save_reader_destroy(&start); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "q3Characters");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), state = ck_f(&value, "state");
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_q3_character_checkpoint checkpoint = qa_app_sim_read_q3_character(&state);
      qa_app_sim_movement_player *restored = qa_app_shared_sim_require_player(sim, actor.id);
      qa_q3_character_actor *character = qa_app_shared_sim_new_q3_character(sim, actor, restored->character_definition.provider,
          checkpoint.product, restored->character_definition.content);
      checkpoint.product = character->product;
      qa_q3_character_actor_restore(character, checkpoint);
      qa_app_shared_sim_set_q3_character(sim, actor, character);
      qa_save_reader_destroy(&state); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
  }
  {
    size_t count = 0;
    const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
    qa_actor_id *actors = qa_app_shared_sim_keep(sim, (count ? count : 1) * sizeof *actors);
    if (count != 0) memcpy(actors, players, count * sizeof *actors);
    for (size_t i = 0; i < count; i++) {
      qa_owned_actor owner;
      if (qa_session_actor_registry_resolve_owned(sim->actors, actors[i], &owner)) qa_app_shared_sim_bind_selected_original_pickups(sim, owner);
    }
  }
  {
    qa_save_reader death = ck_f(&reader, "deathAnimations"), version = ck_f(&death, "version");
    qa_q3_death_animation_checkpoint checkpoint = {qa_save_reader_literal_number(&version, 1), ck_integer_of(&death, "index", 0)};
    qa_q3_death_animation_sequence_restore(&sim->death_animations, checkpoint);
    qa_save_reader_destroy(&version); qa_save_reader_destroy(&death);
  }
  {
    qa_save_reader list = ck_f(&reader, "characterTicks");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i);
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      double time = ck_number_of(&value, "time");
      qa_ss_map_set(&sim->character_ticks, &actor, &time);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "entryCarry");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), state = ck_f(&value, "state");
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_q1_travel_state travel = qa_app_sim_read_q1_travel(&state);
      qa_q1_travel_state *previous = qa_ss_map_get(&sim->entry_carry, &actor);
      if (previous != NULL) qa_q1_travel_state_destroy(previous);
      qa_ss_map_set(&sim->entry_carry, &actor, &travel);
      qa_save_reader_destroy(&state); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "detachedModels");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), content = ck_f(&value, "content"), family = ck_f(&value, "family"),
          skin = ck_f(&value, "skin"), effects = ck_f(&value, "effects");
      qa_owned_actor actor = ck_owner_of(sim, &value, "actor");
      qa_ss_detached_model model; memset(&model, 0, sizeof model);
      model.content = qa_app_shared_sim_intern(sim, qa_read_content_id(&content));
      model.path = qa_app_shared_sim_intern(sim, ck_string_of(&value, "path"));
      if (ck_present(&family)) { static const char *const families[] = {"q1", "q2"}; model.family = qa_save_reader_choice_string(&family, families, 2); }
      if (ck_present(&skin)) { model.has_skin = true; model.skin = qa_save_reader_integer_min(&skin, 0); }
      if (ck_present(&effects)) { model.has_effects = true; model.effects = qa_save_reader_integer_min(&effects, 0); }
      qa_ss_map_set(&sim->detached_models, &actor, &model);
      qa_actor_callbacks callbacks = {sim, ck_release_detached, NULL, NULL, NULL, NULL};
      qa_actor_callback_table_bind(sim->callbacks, actor, callbacks);
      qa_save_reader_destroy(&content); qa_save_reader_destroy(&family); qa_save_reader_destroy(&skin); qa_save_reader_destroy(&effects);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "viewModels");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), q2 = ck_f(&value, "q2"), actor_r = ck_f(&value, "actor");
      qa_ss_view_model model; memset(&model, 0, sizeof model);
      if (!qa_save_reader_is_null(&q2)) {
        model.has_q2 = true;
        model.q2_skin = ck_any_integer_of(&q2, "skin"); model.q2_rate = ck_finite_of(&q2, "rate");
        model.q2_kick_origin = ck_vector_of(&q2, "kickOrigin"); model.q2_kick_angles = ck_vector_of(&q2, "kickAngles");
      }
      qa_actor_id actor = ck_reference(sim, &actor_r);
      model.path = qa_app_shared_sim_intern(sim, ck_string_of(&value, "path"));
      model.frame = ck_number_of(&value, "frame");
      qa_ss_map_set(&sim->view_models, &actor, &model);
      qa_save_reader_destroy(&q2); qa_save_reader_destroy(&actor_r); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "q2Views");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), actor_r = ck_f(&value, "actor"), view_r = ck_f(&value, "view");
      qa_actor_id actor = ck_reference(sim, &actor_r);
      qa_q2_base_player_view view = qa_app_sim_read_q2_view(&view_r);
      if (view.selected_item != NULL) view.selected_item = qa_app_shared_sim_intern(sim, view.selected_item);
      if (view.has_timer) view.timer_item = qa_app_shared_sim_intern(sim, view.timer_item);
      qa_ss_map_set(&sim->q2_views, &actor, &view);
      qa_save_reader_destroy(&view_r); qa_save_reader_destroy(&actor_r); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    list = ck_f(&reader, "sourceModels");
    for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
      qa_save_reader value = ck_i(&list, i), attached_r = ck_f(&value, "attachedModels"), actor_r = ck_f(&value, "actor");
      size_t attached_count = qa_save_reader_list_count(&attached_r);
      const char *attached[3] = {NULL, NULL, NULL};
      for (size_t k = 0; k < attached_count; k++) {
        qa_save_reader entry = ck_i(&attached_r, k);
        const char *text = qa_save_reader_string(&entry);
        if (k < 3) attached[k] = text;
        qa_save_reader_destroy(&entry);
      }
      qa_actor_id actor = ck_reference(sim, &actor_r);
      if (attached_count > 3) qa_save_reader_fail(&value, "Q2 attached models exceed three slots");
      /* Native original saves reconstruct authoritative model slots; older caches compacted empty slots. */
      if (!ck_reconstructed(restore, actor)) {
        if (attached_count != 3) qa_save_reader_fail(&value, "Q2 attached models must retain three slots");
        qa_q2_presentation_event model; memset(&model, 0, sizeof model);
        model.kind = "model"; model.has_actor = true; model.actor = actor;
        model.path = qa_app_shared_sim_intern(sim, ck_string_of(&value, "path"));
        for (int k = 0; k < 3; k++) model.attached_models[k] = qa_app_shared_sim_intern(sim, attached[k]);
        model.frame = ck_number_of(&value, "frame"); model.old_frame = ck_number_of(&value, "oldFrame");
        model.scale = ck_number_of(&value, "scale"); model.skin = ck_number_of(&value, "skin");
        model.effects = ck_number_of(&value, "effects"); model.render_flags = ck_number_of(&value, "renderFlags");
        qa_save_reader alpha = ck_f(&value, "alpha");
        model.alpha = ck_present(&alpha) ? qa_save_reader_finite(&alpha) : 1;
        qa_save_reader_destroy(&alpha);
        qa_ss_map_set(&sim->source_models, &actor, &model);
        bool read_order = true;
        qa_ss_map_set(&sim->source_model_read_order, &actor, &read_order);
      }
      qa_save_reader_destroy(&attached_r); qa_save_reader_destroy(&actor_r); qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
  }
  {
    qa_save_reader change = ck_f(&reader, "levelChange");
    free(sim->level_change_map); sim->level_change_map = NULL;
    sim->has_level_change = !qa_save_reader_is_null(&change);
    sim->has_level_change_landmark = false;
    if (sim->has_level_change) {
      sim->level_change_map = qa_ss_strdup(ck_string_of(&change, "map"));
      sim->level_change_server_flags = ck_number_of(&change, "serverFlags");
      qa_save_reader landmark = ck_f(&change, "landmark");
      if (!qa_save_reader_is_null(&landmark)) {
        qa_save_reader player = ck_f(&landmark, "player");
        qa_q2_landmark_carry carry;
        carry.player = ck_reference(sim, &player);
        carry.name = qa_app_shared_sim_intern(sim, ck_string_of(&landmark, "name"));
        carry.relative_origin = ck_vector_of(&landmark, "relativeOrigin");
        carry.relative_velocity = ck_vector_of(&landmark, "relativeVelocity");
        carry.relative_view_angles = ck_vector_of(&landmark, "relativeViewAngles");
        sim->has_level_change_landmark = true; sim->level_change_landmark = carry;
        qa_save_reader_destroy(&player);
      }
      qa_save_reader_destroy(&landmark);
    }
    qa_save_reader_destroy(&change);
  }
  {
    qa_save_reader physics = ck_f(&reader, "physics");
    qa_app_sim_shared_physics_restore_checkpoint(sim->physics, &physics, sim->strategy->keeps_angle_deltas(sim), ck_reconstructed, restore);
    qa_save_reader portals = ck_f(&reader, "portals");
    for (size_t i = 0, n = qa_save_reader_list_count(&portals); i < n; i++) {
      qa_save_reader value = ck_i(&portals, i);
      double portal = ck_integer_of(&value, "portal", 0); bool open = ck_bool_of(&value, "open");
      qa_ss_map_set(&sim->area_portals, &portal, &open);
      qa_shared_set_area_portal_state(sim->scene, portal, open);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&portals);
    qa_shared_body_link_host links = {restore->hosts.actors, restore->hosts.bodies, restore, ck_link_storage};
    qa_restore_shared_body_links(save, &links);
    qa_app_sim_shared_physics_restore_spatial(sim->physics, &physics, ck_reconstructed, restore);
    qa_save_reader_destroy(&physics);
  }
  sim->strategy->restore_gravity(sim, save);
  for (size_t i = 0; i < save->think_count; i++) {
    const qa_think_checkpoint *think = &save->thinks[i];
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(sim->actors, think->actor, &actor)) ck_fail("Saved think has no restored actor");
    qa_think_timing timing; memset(&timing, 0, sizeof timing);
    timing.due = think->due; timing.boundary = think->boundary;
    timing.has_execution_provider = think->has_execution_provider;
    timing.execution_provider = think->has_execution_provider ? qa_app_shared_sim_intern(sim, think->execution_provider) : NULL;
    timing.order.actor = actor.id; timing.order.provider = qa_app_shared_sim_intern(sim, think->provider); timing.order.sequence = think->sequence;
    qa_frame_scheduler_schedule(sim->scheduler, actor, qa_app_shared_sim_intern(sim, think->callback), timing);
  }
  {
    qa_save_reader punch = ck_f(&reader, "q1Punch");
    qa_app_sim_q1_player_punch_restore(sim->q1_punch, &punch);
    if (!ck_present(&punch) && sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q1")
        && weapon_source != NULL && qa_ss_is(weapon_source->kind, "q1")) {
      size_t count = 0;
      const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
      for (size_t i = 0; i < count; i++) {
        qa_q1_player_state *previous = NULL;
        for (size_t k = 0; k < weapon_source->q1_game->player_count && previous == NULL; k++)
          if (qa_same_actor(weapon_source->q1_game->players[k]->actor.id, players[i])) previous = weapon_source->q1_game->players[k];
        if (previous != NULL) qa_app_sim_q1_player_punch_write(sim->q1_punch, players[i], previous->punch_angles);
      }
    }
    qa_save_reader_destroy(&punch);
  }
  {
    qa_save_reader dropped = ck_f(&reader, "droppedPickups");
    qa_app_sim_source_pickup_cargo_restore(sim->dropped_pickups, &dropped);
    qa_save_reader_destroy(&dropped);
    qa_save_reader behaviors = ck_f(&reader, "weaponBehaviors");
    qa_app_sim_simulation_weapon_behaviors_restore(sim->weapon_behavior, &behaviors);
    qa_save_reader_destroy(&behaviors);
    qa_save_reader ordinal = ck_f(&reader, "modClientApplicationOrdinal");
    qa_mod_client_applications_restore(sim->mod_client_applications, ck_present(&ordinal) ? (int64_t)qa_save_reader_integer_min(&ordinal, 0) : 0);
    qa_save_reader_destroy(&ordinal);
    qa_save_reader commands = ck_f(&reader, "modClientCommands");
    qa_app_sim_mod_client_command_host host = {&sim->options.identity, sim, ck_reference_saved, ck_mod_client};
    qa_app_sim_mod_client_commands read = qa_app_sim_read_mod_client_commands(&commands, host);
    for (size_t i = 0; i < read.count; i++) {
      qa_mod_client_command_receipt receipt = read.commands[i];
      qa_ss_map_set(&sim->mod_client_commands, &receipt.input.actor, &receipt);
    }
    if (read.count != 0) keep_tree = true; /* receipts borrow the tree's strings */
    ck_own(sim, ck_dispose_mod_commands, ck_keep_mod_commands(read));
    qa_save_reader_destroy(&commands);
    qa_save_reader events = ck_f(&reader, "events");
    qa_app_shared_sim_events_restore(sim, &events, ck_reference_saved, sim);
    qa_save_reader_destroy(&events);
  }
  qa_app_shared_sim_resume_q2_presentation(sim, NULL);
  {
    qa_save_reader timers = ck_f(&reader, "selectedAmmoTimers");
    if (sim->selected_ammo_timers != NULL && (sim->strategy->as_q3(sim) != NULL)) {
      size_t count = 0;
      const qa_owned_actor *actors = qa_app_shared_sim_selected_ammo_actors(sim, &count);
      restore->ammo_timers = &timers;
      qa_app_q3_mapped_ammo_regeneration_restore(sim->selected_ammo_timers, &timers, actors, count, ck_legacy_ammo_time, restore);
      restore->ammo_timers = NULL;
    } else if (ck_given(&timers)) qa_save_reader_fail(&timers, "Saved ammo timers have no selected source owner");
    qa_save_reader_destroy(&timers);
  }
  if (qa_app_sim_simulation_events_next_sequence(sim->events) != save->next_event_sequence)
    ck_fail("Save event sequence disagrees with its source journal");
  if (has_saved_monsters) qa_app_sim_selected_monsters_checkpoint_free(&saved_monsters);
  if (!restore->hydrate_q1) qa_save_reader_destroy(&restore->selected_source);
  qa_save_reader_destroy(&reader);
  if (keep_tree) ck_own(sim, ck_dispose_tree, tree);
  else qa_save_value_free(tree);
}
