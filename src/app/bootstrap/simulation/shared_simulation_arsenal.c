/* Port of src/app/bootstrap/simulation/runtime.ts, SharedSimulation phase
 * B2 region: selected arsenals, weapons, pickups, equipment, grapple, hand
 * grenades, inventory UI and presentation (docs/runtime_layout.md, group
 * B2). Every method keeps its TS name in a comment and every branch in TS
 * order. TS throws are qa_raise("Error", ...) with the TS message.
 *
 * Ownership: selected arsenal reads return malloc'd ammo arrays (the arsenal
 * modules' contract); reads used only inside a method are freed there, and
 * the read a player holds is adopted by qa_app_shared_sim_store_player_arsenal.
 * Arrays a TS method returns are thread-local scratch that stays valid until
 * the next call of the same method (the pattern of players()).
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/app/bootstrap/simulation/quakec_character_animation.h"
#include "qa/compat/q2/classic/pickup_profile.h"
#include "qa/compat/q2/classic/world_profile.h"
#include "qa/compat/q2/rerelease/pickup_profile.h"
#include "qa/compat/q2/rerelease/world_profile.h"
#include "qa/compat/q2/rerelease/debug_shapes.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/composition/expansion_source_supply.h"
#include "qa/content/composition/expansion_supply.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/composition/q2_q1_supply.h"
#include "qa/content/composition/q2_q3_supply.h"
#include "qa/content/composition/q3_q1_supply.h"
#include "qa/content/composition/q3_q2_supply.h"
#include "qa/movement/q3/animation.h"
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include "qa/formats/q2_map.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/app/bootstrap/simulation/powerup_timers.h"
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/app/bootstrap/simulation/q3/guest_player.h"
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_player.h"
#include "qa/app/bootstrap/simulation/weapon_slot_projection.h"
#include "qa/app/bootstrap/simulation/arsenal/inventory_labels.h"
#include "qa/app/bootstrap/simulation/q3/player_state.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/content/q3/team_arena/client_events.h"
#include "qa/content/q2/multiplayer/ctf/index.h"
#include "qa/content/q2/multiplayer/ctf/flags.h"
#include "qa/content/q2/multiplayer/lmctf/runtime.h"
#include "qa/content/q2/multiplayer/lmctf/flags.h"
#include "qa/content/q1/addons/ctf/flags.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/equipment/hand_grenades.h"
#include "qa/compat/q2/native_primary_player.h"
#include "qa/compat/q2/rerelease/pickup_protection.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/game_combat.h"
#include "qa/app/bootstrap/simulation/native_q2_map.h"
#include "qa/app/bootstrap/q2_localization.h"
#include "qa/text/localization_resources.h"
#include "qa/content/catalog/timing.h"
#include "qa/guest/runtime/windows/contracts.h"
#include "qa/compat/q2/classic/host.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/content/composition/q1/commands.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/core/cvars/info.h"
#include "qa/content/composition/q2_expansion_arsenal.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q3/base/game/death.h"
#include "qa/content/q3/team_arena/team.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/host.h"

static noreturn void ss_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *ss_grow(void *ptr, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return ptr;
  size_t next = *capacity ? *capacity * 2 : 8;
  while (next < need) next *= 2;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
static bool ss_owned(qa_app_shared_simulation *sim, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(sim->actors, actor, out);
}
static qa_shared_body_table *ss_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
/* TS this.combat.read(actor)?.health ?? fallback. */
static double ss_health_or(qa_app_shared_simulation *sim, qa_actor_id actor, double fallback) {
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  return combat.has_value ? combat.value.health : fallback;
}
static double ss_count(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(sim->inventory, actor, item);
}
static void ss_configure(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_inventory_entry entry) {
  qa_shared_inventory_table_configure(sim->inventory, actor, entry, NULL, NULL);
}
/* TS this.inventory.entries(actor).find(entry => entry.item === item). */
static const qa_inventory_entry *ss_entry(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
  for (size_t i = 0; i < count; i++) if (qa_ss_is(entries[i].item, item)) return &entries[i];
  return NULL;
}
/* The strategy's typed access (TS this.strategy.asQ1() !== null ...) for one source kind. */
static bool ss_source_is(qa_app_shared_simulation *sim, qa_ss_source_kind kind) {
  switch (kind) {
    case QA_SS_SOURCE_Q1: return sim->strategy->as_q1(sim) != NULL;
    case QA_SS_SOURCE_Q2: return sim->strategy->as_q2(sim) != NULL;
    case QA_SS_SOURCE_QUAKEC: return sim->strategy->as_quakec(sim) != NULL;
    case QA_SS_SOURCE_Q3: return sim->strategy->as_q3(sim) != NULL;
    case QA_SS_SOURCE_Q3_QVM: return sim->strategy->as_q3_qvm(sim) != NULL;
    case QA_SS_SOURCE_Q2_NATIVE: return sim->strategy->as_q2_native(sim) != NULL;
    case QA_SS_SOURCE_LOADING: return qa_ss_is(sim->strategy->kind, "loading");
  }
  return false;
}
static bool ss_family_is(const qa_app_shared_simulation *sim, const char *family) {
  return sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, family);
}
static qa_app_sim_weapon_slot *ss_slot(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  return slot ? *slot : NULL;
}
/* TS this.weaponSlots.get(actor)?.primarySelected() ?? true. */
static bool ss_primary_selected(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  return slot ? qa_app_sim_weapon_slot_primary_selected(slot) : true;
}
/* TS this.weaponSlots.get(actor)?.reconcile(). */
static void ss_reconcile(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  if (slot) qa_app_sim_weapon_slot_reconcile(slot);
}
static qa_source_time ss_milliseconds(double value) { qa_source_time time = {QA_SOURCE_TIME_MILLISECONDS, value}; return time; }

/* this.inventory as the selected arsenals' SharedInventoryTable surface
 * (entries/count/configure/give). */
static const qa_inventory_entry *ss_access_entries(void *ctx, qa_actor_id actor, size_t *out_count) {
  return qa_shared_inventory_table_entries(((qa_app_shared_simulation *)ctx)->inventory, actor, out_count);
}
static double ss_access_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(((qa_app_shared_simulation *)ctx)->inventory, actor, item);
}
static void ss_access_configure(void *ctx, qa_owned_actor actor, qa_inventory_entry entry) {
  qa_shared_inventory_table_configure(((qa_app_shared_simulation *)ctx)->inventory, actor, entry, NULL, NULL);
}
static double ss_access_give(void *ctx, qa_owned_actor actor, qa_item_id item, double count) {
  return qa_shared_inventory_table_give(((qa_app_shared_simulation *)ctx)->inventory, actor, item, count);
}
qa_app_inventory_access qa_app_shared_sim_inventory_access(qa_app_shared_simulation *sim) {
  qa_app_inventory_access access = {sim, ss_access_entries, ss_access_count, ss_access_configure, ss_access_give};
  return access;
}

/* ---- selectedArsenal: the Q1 | Q2 | Q3 family union ---- */
static bool ss_sel_has(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!sim->has_selected_arsenal) return false;
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_has(sim->selected_arsenal.q1, actor);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_has(sim->selected_arsenal.q2, actor);
  return qa_app_q3_selected_arsenal_has(sim->selected_arsenal.q3, actor);
}
/* read(actor): the ammo array is malloc'd (free it). */
static qa_arsenal_state ss_sel_read(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_read(sim->selected_arsenal.q1, actor);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_read(sim->selected_arsenal.q2, actor);
  return qa_app_q3_selected_arsenal_read(sim->selected_arsenal.q3, actor);
}
static void ss_arsenal_free(qa_arsenal_state *state) { free((void *)state->ammo); state->ammo = NULL; state->ammo_count = 0; }
static void ss_arsenal_free_cleanup(void *state) { ss_arsenal_free(state); }
/* admit(owner, maximum, team): Q1 and Q2 take no team argument. */
static qa_arsenal_state ss_sel_admit(qa_app_shared_simulation *sim, qa_owned_actor actor, double maximum, bool team) {
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_admit(sim->selected_arsenal.q1, actor, maximum);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_admit(sim->selected_arsenal.q2, actor, maximum);
  return qa_app_q3_selected_arsenal_admit(sim->selected_arsenal.q3, actor, maximum, team);
}
static void ss_sel_remove(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->selected_arsenal.q1) qa_app_q1_selected_arsenal_remove(sim->selected_arsenal.q1, actor);
  else if (sim->selected_arsenal.q2) qa_app_q2_selected_arsenal_remove(sim->selected_arsenal.q2, actor);
  else qa_app_q3_selected_arsenal_remove(sim->selected_arsenal.q3, actor);
}
static bool ss_sel_select(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_select(sim->selected_arsenal.q1, actor, item);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_select(sim->selected_arsenal.q2, actor, item);
  return qa_app_q3_selected_arsenal_select(sim->selected_arsenal.q3, actor, item);
}
static qa_item_id ss_sel_pending(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_pending_weapon(sim->selected_arsenal.q1, actor);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_pending_weapon(sim->selected_arsenal.q2, actor);
  return qa_app_q3_selected_arsenal_pending_weapon(sim->selected_arsenal.q3, actor);
}
static qa_app_primary_weapon_handoff ss_sel_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_handoff(sim->selected_arsenal.q1, actor);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_handoff(sim->selected_arsenal.q2, actor);
  return qa_app_q3_selected_arsenal_handoff(sim->selected_arsenal.q3, actor);
}
static void ss_sel_pickup_weapons(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_item_id *weapons, size_t count,
    qa_pickup_selection selection) {
  if (sim->selected_arsenal.q1) qa_app_q1_selected_arsenal_pickup_weapons(sim->selected_arsenal.q1, actor, weapons, count, selection);
  else if (sim->selected_arsenal.q2) qa_app_q2_selected_arsenal_pickup_weapons(sim->selected_arsenal.q2, actor, weapons, count, selection);
  else qa_app_q3_selected_arsenal_pickup_weapons(sim->selected_arsenal.q3, actor, weapons, count, selection);
}
static qa_weapon_step_result ss_sel_step(qa_app_shared_simulation *sim, qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  if (sim->selected_arsenal.q1) return qa_app_q1_selected_arsenal_step(sim->selected_arsenal.q1, input, intent);
  if (sim->selected_arsenal.q2) return qa_app_q2_selected_arsenal_step(sim->selected_arsenal.q2, input, intent);
  return qa_app_q3_selected_arsenal_step(sim->selected_arsenal.q3, input, intent);
}
/* catalog(): thread-local scratch, valid until the next call. */
static const qa_selected_pickup_weapon *ss_sel_catalog(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_selected_pickup_weapon storage[64];
  size_t count = sim->selected_arsenal.q1 ? qa_app_q1_selected_arsenal_catalog(sim->selected_arsenal.q1, storage, 64)
      : sim->selected_arsenal.q2 ? qa_app_q2_selected_arsenal_catalog(sim->selected_arsenal.q2, storage, 64)
      : qa_app_q3_selected_arsenal_catalog(sim->selected_arsenal.q3, storage, 64);
  *out_count = count < 64 ? count : 64;
  return storage;
}
/* ui(actor, source): items and labels in caller storage. */
#define SS_UI_ITEMS 96
typedef struct ss_ui_storage { qa_app_player_ui_item items[SS_UI_ITEMS]; char labels[SS_UI_ITEMS + 1][64]; } ss_ui_storage;
static qa_app_arsenal_ui ss_sel_ui(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_reference source, ss_ui_storage *storage) {
  if (sim->selected_arsenal.q1)
    return qa_app_q1_selected_arsenal_ui(sim->selected_arsenal.q1, actor, source, storage->items, SS_UI_ITEMS, storage->labels, SS_UI_ITEMS + 1);
  if (sim->selected_arsenal.q2)
    return qa_app_q2_selected_arsenal_ui(sim->selected_arsenal.q2, actor, source, storage->items, SS_UI_ITEMS, storage->labels, SS_UI_ITEMS + 1);
  return qa_app_q3_selected_arsenal_ui(sim->selected_arsenal.q3, actor, source, storage->items, SS_UI_ITEMS, storage->labels, SS_UI_ITEMS + 1);
}

/* The selectedArsenal members phase A calls. */
bool qa_app_shared_sim_selected_arsenal_select(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  return ss_sel_select(sim, actor, item);
}
bool qa_app_shared_sim_selected_arsenal_has(qa_app_shared_simulation *sim, qa_actor_id actor) { return ss_sel_has(sim, actor); }
void qa_app_shared_sim_selected_arsenal_remove(qa_app_shared_simulation *sim, qa_actor_id actor) { ss_sel_remove(sim, actor); }
qa_arsenal_state qa_app_shared_sim_selected_arsenal_admit(qa_app_shared_simulation *sim, qa_owned_actor actor, double health, bool restored) {
  return ss_sel_admit(sim, actor, health, restored);
}
/* selectedArsenal.game (the Q1 family's Q1EntityServices). */
qa_q1_services *qa_app_shared_sim_selected_q1_game(qa_app_shared_simulation *sim) {
  if (!ss_family_is(sim, "q1") || !sim->has_selected_weapon_source || !qa_ss_is(sim->selected_weapon_source.kind, "q1"))
    ss_fail("Selected Q1 arsenal has no source game");
  return sim->selected_weapon_source.q1_game;
}

/* ---- replacedSupplyItems (274-277) ---- */
qa_item_id *qa_ss_replaced_supply_items(const qa_pickup_supply_profile *profile, size_t *out_count) {
  size_t total = profile->ammo_count + profile->weapon_count;
  qa_item_id *result = calloc(total ? total : 1, sizeof *result);
  if (!result) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < total; i++) {
    const qa_pickup_supply_mapping *mapping = i < profile->ammo_count ? &profile->ammo[i] : &profile->weapons[i - profile->ammo_count];
    bool retained = false;
    for (size_t j = 0; j < total && !retained; j++) {
      const qa_pickup_supply_mapping *other = j < profile->ammo_count ? &profile->ammo[j] : &profile->weapons[j - profile->ammo_count];
      for (size_t k = 0; k < other->destination_count; k++) if (qa_ss_is(other->destinations[k], mapping->source)) { retained = true; break; }
    }
    if (!retained) result[count++] = mapping->source;
  }
  *out_count = count;
  return result;
}

/* ---- presentation (7426-7546) ---- */
/* weaponPresentationClock(): false = TS null. */
bool qa_app_shared_sim_weapon_presentation_clock(qa_app_shared_simulation *sim, qa_content_id *out_content, double *out_milliseconds) {
  if (!ss_family_is(sim, "q3")) return false;
  *out_content = sim->weapon_provider.content;
  *out_milliseconds = trunc(sim->selected_milliseconds);
  return true;
}
/* registerResource(content, path, resource) */
void qa_app_shared_sim_register_resource(qa_app_shared_simulation *sim, qa_content_id content, const char *path,
    qa_resolved_resource_reference resource) {
  qa_app_sim_simulation_events_register_resource(sim->events, content, path, resource);
}
/* submitDebugShapes(event) */
void qa_app_shared_sim_submit_debug_shapes(qa_app_shared_simulation *sim, qa_compat_q2_rerelease_debug_shapes_event event) {
  qa_app_shared_sim_assert_open(sim);
  if (!qa_world_debug_line_store_submit(&sim->debug_line_store, event.lines, event.line_count,
          qa_js_round(qa_app_shared_sim_time_seconds(sim) * 1000), (int64_t)event.lifetime_milliseconds))
    qa_range_error("Out of memory");
}
/* beginPresentationFrame(frame) */
void qa_app_shared_sim_begin_presentation_frame(qa_app_shared_simulation *sim, double frame) {
  qa_app_shared_sim_assert_open(sim);
  if (sim->debug_line_frame == frame) return;
  sim->debug_line_frame = frame;
  sim->debug_line_snapshot = qa_world_debug_line_store_snapshot(&sim->debug_line_store,
      qa_js_round(qa_app_shared_sim_time_seconds(sim) * 1000), frame, &sim->debug_line_snapshot_count);
}
/* debugLines() */
const qa_debug_line *qa_app_shared_sim_debug_lines(qa_app_shared_simulation *sim, size_t *out_count) {
  *out_count = sim->debug_line_snapshot_count;
  return sim->debug_line_snapshot;
}
/* worldText(): the store hands out views; the array of values is scratch. */
const qa_world_text *qa_app_shared_sim_world_text(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_world_text *storage; static __thread size_t capacity;
  if (sim->world_text_frame != sim->source_frame.frame) {
    sim->world_text_frame = sim->source_frame.frame;
    sim->world_text_snapshot = qa_world_text_store_snapshot(&sim->world_text_store, qa_app_shared_sim_time_seconds(sim),
        sim->source_frame.frame, &sim->world_text_snapshot_count);
  }
  storage = ss_grow(storage, &capacity, sim->world_text_snapshot_count + 1, sizeof *storage);
  for (size_t i = 0; i < sim->world_text_snapshot_count; i++) storage[i] = *sim->world_text_snapshot[i];
  *out_count = sim->world_text_snapshot_count;
  return storage;
}
/* drainPresentationEvents(): events.takePresentation() (PresentationState super). */
const qa_app_sim_presentation_event *const *qa_app_shared_sim_drain_presentation_events(qa_app_shared_simulation *sim, size_t *out_count) {
  return qa_app_presentation_state_take_presentation(sim->presentation, out_count);
}
/* capturePresentationSnapshot() */
static void ss_capture_pose(qa_app_shared_simulation *sim, qa_ss_map *snapshot, qa_actor_id actor) {
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor, &body)) return;
  qa_ss_view_model *view = qa_ss_map_get(&sim->view_models, &actor);
  qa_ss_presentation_pose pose = {body.origin, body.angles, view ? view->path : NULL, view ? view->frame : 0};
  qa_ss_map_set(snapshot, &actor, &pose);
}
void qa_app_shared_sim_capture_presentation_snapshot(qa_app_shared_simulation *sim) {
  qa_ss_map snapshot; qa_ss_map_init(&snapshot, QA_SS_KEY_ACTOR, sizeof(qa_ss_presentation_pose));
  for (size_t i = 0; i < sim->actor_executions.count; i++) {
    const qa_app_sim_actor_execution *entry = qa_ss_map_value_at(&sim->actor_executions, i);
    if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) ss_capture_pose(sim, &snapshot, entry->q1_entity->actor.id);
    else if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) ss_capture_pose(sim, &snapshot, entry->q2_entity->actor.id);
  }
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    ss_capture_pose(sim, &snapshot, player->actor.id);
  }
  qa_ss_map_free(&sim->presentation_snapshot);
  sim->presentation_snapshot = snapshot;
}
/* presentationFraction() */
qa_ss_presentation_fraction qa_app_shared_sim_presentation_fraction(qa_app_shared_simulation *sim) {
  qa_ss_presentation_fraction result = {1, 0};
  double tick = sim->presentation_tick_milliseconds;
  if (!sim->has_presentation_tick_milliseconds || tick <= 0 || sim->presentation_snapshot.count == 0) return result;
  double end = qa_app_shared_sim_time_seconds(sim) * 1000;
  double fraction = qa_js_min(1, qa_js_max(0, (sim->source_scheduling_milliseconds - (sim->presentation_ahead ? end - tick : end)) / tick));
  result.fraction = fraction; result.back_lerp = 1 - fraction;
  return result;
}
/* interpolatedPose(actor, origin, angles, fraction) */
static double ss_lerp_angle(double from, double to, double fraction) {
  double d = fmod(to - from, 360);
  if (d > 180) d -= 360;
  else if (d < -180) d += 360;
  return from + d * fraction;
}
qa_ss_interpolated_pose qa_app_shared_sim_interpolated_pose(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin,
    qa_vec3 angles, double fraction) {
  qa_ss_interpolated_pose pose = {origin, angles, origin, 0};
  const qa_ss_presentation_pose *previous = qa_ss_map_get(&sim->presentation_snapshot, &actor);
  if (previous == NULL || fraction >= 1) return pose;
  qa_vec3 delta = {origin.x - previous->origin.x, origin.y - previous->origin.y, origin.z - previous->origin.z};
  if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 512 * 512) return pose;
  pose.origin.x = previous->origin.x + delta.x * fraction;
  pose.origin.y = previous->origin.y + delta.y * fraction;
  pose.origin.z = previous->origin.z + delta.z * fraction;
  pose.angles.x = ss_lerp_angle(previous->angles.x, angles.x, fraction);
  pose.angles.y = ss_lerp_angle(previous->angles.y, angles.y, fraction);
  pose.angles.z = ss_lerp_angle(previous->angles.z, angles.z, fraction);
  pose.previous_origin = previous->origin;
  pose.back_lerp = 1 - fraction;
  return pose;
}
/* q1VisualFields(alpha, scale) */
qa_ss_visual_fields qa_app_shared_sim_q1_visual_fields(double alpha, double scale) {
  qa_ss_visual_fields fields;
  fields.alpha = alpha == 0 ? 1 : qa_js_max(0, qa_js_min(1, (double)(float)alpha));
  fields.scale = scale == 0 ? 1 : (double)(float)scale;
  return fields;
}

/* ---- source views shared by the methods below ---- */
static qa_app_q3_guest_records *ss_qvm_records(qa_app_shared_simulation *sim) { return qa_app_q3_qvm_server_game_records(sim->source.qvm); }
/* this.source.game.records.player(slot), with the stat arrays kept. */
typedef struct ss_qvm_state { qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16]; } ss_qvm_state;
static void ss_qvm_state_read(qa_app_shared_simulation *sim, double slot, ss_qvm_state *out) {
  qa_app_q3_guest_records_player(ss_qvm_records(sim), slot, &out->state, out->stats, out->persistent, out->powerups, out->ammo);
  out->state.stats = (qa_double_view){out->stats, 16}; out->state.persistent = (qa_double_view){out->persistent, 16};
  out->state.powerups = (qa_double_view){out->powerups, 16}; out->state.ammo = (qa_double_view){out->ammo, 16};
}
/* TS this.source.game.records.player(this.source.game.records.requireSlot(actor)). */
static void ss_qvm_actor_state(qa_app_shared_simulation *sim, qa_actor_id actor, ss_qvm_state *out) {
  ss_qvm_state_read(sim, qa_app_q3_guest_records_require_slot(ss_qvm_records(sim), actor), out);
}
/* this.source.game.players().find(entry => entry.actor.equals(actor)) */
static bool ss_qvm_player_of(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_q3_player *out) {
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  for (size_t i = 0; i < total && i < 64; i++) if (qa_same_actor(players[i].actor, actor)) { *out = players[i]; return true; }
  return false;
}

/* ---- q1CombatSource (2063-2069) ---- */
qa_q1_services *qa_app_shared_sim_q1_combat_source(qa_app_shared_simulation *sim) {
  if (ss_source_is(sim, QA_SS_SOURCE_Q1)) return sim->source.q1_game;
  if (ss_family_is(sim, "q1")) return qa_app_shared_sim_selected_q1_game(sim);
  /* A QuakeC program applies Q1 rules to foreign targets through the shared policy alone. */
  if (ss_source_is(sim, QA_SS_SOURCE_QUAKEC)) return NULL;
  ss_fail("Q1 combat source is not attached");
}
/* ---- selectedQ1Frame (2071-2073) ---- */
qa_frame_context qa_app_shared_sim_selected_q1_frame(qa_app_shared_simulation *sim) {
  return qa_app_sim_provider_frame(sim->source_frame, qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock,
      qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider).clock);
}
/* ---- requireQvmWeapons (2075-2078) ---- */
qa_qvm_primary_weapons *qa_app_shared_sim_require_qvm_weapons(qa_app_shared_simulation *sim) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3_QVM) || sim->source.qvm_weapons == NULL) ss_fail("Original QVM weapon services have no qualified owner");
  return sim->source.qvm_weapons;
}
/* ---- refreshArsenalCache (2079-2082) ---- */
void qa_app_shared_sim_refresh_arsenal_cache(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player != NULL && sim->has_selected_arsenal) qa_app_shared_sim_store_player_arsenal(sim, player, ss_sel_read(sim, actor));
}
/* ---- requireNativeWeapons (2097-2100) ---- */
qa_compat_q2_native_primary_weapons *qa_app_shared_sim_require_native_weapons(qa_app_shared_simulation *sim) {
  if (sim->native_primary_weapons == NULL) ss_fail("Original native selected weapons have no qualified owner");
  return sim->native_primary_weapons;
}
/* ---- arsenalObservation (2083-2096) ---- */
bool qa_app_shared_sim_arsenal_observation(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_arsenal_observation *out) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player != NULL) {
    out->actor = player->actor; out->view_angles = player->view_angles; out->view_height = player->view_height; out->water_level = player->water_level;
    return true;
  }
  qa_client_id client;
  if (ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE)) {
    qa_owned_actor owner;
    if (!ss_owned(sim, actor, &owner) || !qa_app_shared_sim_player_client(sim, actor, &client)) return false;
    qa_compat_q2_native_primary_weapon_input input = qa_compat_q2_native_primary_weapons_input(qa_app_shared_sim_require_native_weapons(sim), actor);
    out->actor = owner; out->view_angles = input.view_angles; out->view_height = input.view_height; out->water_level = input.water_level;
    return true;
  }
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3_QVM)) return false;
  qa_owned_actor owner; double slot;
  bool owned = ss_owned(sim, actor, &owner);
  bool has_slot = qa_app_q3_guest_records_slot(ss_qvm_records(sim), actor, &slot);
  if (!owned || !has_slot || !qa_app_shared_sim_player_client(sim, actor, &client)) return false;
  ss_qvm_state state; ss_qvm_state_read(sim, slot, &state);
  out->actor = owner; out->view_angles = state.state.view_angles; out->view_height = state.state.view_height;
  out->water_level = qa_qvm_primary_weapons_water_level(qa_app_shared_sim_require_qvm_weapons(sim), actor);
  return true;
}

/* ---- qcDamageModifier (2192-2196) ---- */
static double ss_qc_damage_transform(void *ctx, const qa_actor_id *attacker, double amount) {
  qa_app_shared_simulation *sim = ctx;
  return qa_app_sim_quakec_source_damage_amount(sim->source.quakec, attacker, amount);
}
/* QuakeCStrategy.damageModifier(): owner host.entitiesProvider, transform game.damageAmount. */
bool qa_ss_quakec_damage_modifier(qa_app_shared_simulation *sim, qa_source_damage_modifier *out) {
  memset(out, 0, sizeof *out);
  out->owner = sim->recipe.map.entities.provider; out->ctx = sim; out->transform = ss_qc_damage_transform;
  return true;
}
/* damageModifierOption(): *storage holds the modifier; NULL when the source has none. */
const qa_source_damage_modifier *qa_app_shared_sim_damage_modifier_option(qa_app_shared_simulation *sim, qa_source_damage_modifier *storage) {
  return sim->strategy->damage_modifier(sim, storage) ? storage : NULL;
}

/* ---- nativePrimary (2434) ----
 * preparedNativePrimary(prepared): the declared profile, else the builtin
 * profile for the artifact digest, cached per prepared guest (TS WeakMap). */
static const void *ss_pickups_classic(void *ctx, qa_content_digest digest, const qa_compat_q2_native_pickup_profile **out) {
  (void)ctx; *out = qa_compat_q2_classic_get_pickup_profile(digest); return *out;
}
static const void *ss_pickups_rerelease(void *ctx, qa_content_digest digest, const qa_compat_q2_native_pickup_profile **out) {
  (void)ctx; *out = qa_compat_q2_rerelease_get_pickup_profile(digest); return *out;
}
typedef struct ss_builtin_primary {
  const void *prepared; bool found;
  qa_compat_q2_native_primary_profile profile;
  qa_compat_q2_classic_world_profile classic_world;
} ss_builtin_primary;
static __thread ss_builtin_primary ss_builtin_primaries[8];
static __thread size_t ss_builtin_primary_count;
static const void *ss_world_classic(void *ctx, qa_content_digest digest) {
  ss_builtin_primary *entry = ctx;
  return qa_compat_q2_classic_get_primary_world_profile(digest, &entry->classic_world) ? (const void *)&entry->classic_world : NULL;
}
static const void *ss_world_rerelease(void *ctx, qa_content_digest digest) {
  (void)ctx; return qa_compat_q2_rerelease_get_primary_world_profile(digest);
}
const qa_compat_q2_native_primary_profile *qa_app_shared_sim_native_primary(qa_app_shared_simulation *sim) {
  const qa_app_sim_native_q2_guest_options *guest = qa_app_shared_sim_q2_guest(sim);
  if (guest == NULL) return NULL;
  bool classic = qa_ss_is(guest->edition, "classic");
  const qa_app_sim_prepared_classic_guest *classic_prepared = classic ? guest->prepared : NULL;
  const qa_app_sim_prepared_rerelease_guest *rerelease_prepared = classic ? NULL : guest->prepared;
  if (classic ? classic_prepared->has_primary : rerelease_prepared->has_primary)
    return classic ? &classic_prepared->primary.profile : &rerelease_prepared->primary.profile;
  for (size_t i = 0; i < ss_builtin_primary_count; i++)
    if (ss_builtin_primaries[i].prepared == guest->prepared) return ss_builtin_primaries[i].found ? &ss_builtin_primaries[i].profile : NULL;
  if (ss_builtin_primary_count == 8) ss_fail("Native primary cache is full");
  ss_builtin_primary *entry = &ss_builtin_primaries[ss_builtin_primary_count++];
  memset(entry, 0, sizeof *entry);
  entry->prepared = guest->prepared;
  const qa_resolved_resource_reference *artifact = classic ? classic_prepared->execution.artifact : rerelease_prepared->execution.artifact;
  qa_compat_q2_primary_world_providers providers; memset(&providers, 0, sizeof providers);
  providers.ctx = entry; providers.pickups_classic = ss_pickups_classic; providers.pickups_rerelease = ss_pickups_rerelease;
  providers.world_classic = ss_world_classic; providers.world_rerelease = ss_world_rerelease;
  entry->found = qa_compat_q2_builtin_native_primary(artifact->digest, guest->edition, providers, &entry->profile);
  return entry->found ? &entry->profile : NULL;
}

/* ---- powerup (4100-4104) ---- */
void qa_app_shared_sim_powerup(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_powerup powerup, double expires) {
  if (!qa_ss_is(powerup, "invulnerability")) return;
  qa_combat_trait_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_invulnerable = true; changes.invulnerable = expires > qa_app_shared_sim_time_seconds(sim) || sim->strategy->god_mode(sim, actor.id);
  qa_gameplay_authority_set_traits(sim->combat, actor, changes);
}
/* ---- weaponEvent (4106-4116) ---- */
void qa_app_shared_sim_weapon_event(qa_app_shared_simulation *sim, qa_content_id content, const qa_q2w_event *event) {
  if (sim->strategy->weapon_event(sim, event)) {
    /* the Q2 source animated its own player */
  } else if (event->kind == QA_Q2W_EVENT_PLAYER_ANIMATION) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, *event->actor);
    if (player != NULL) {
      qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &player->actor);
      if (character) qa_q2_character_actor_set_animation(*character, event->priority, event->first, event->last);
    }
  }
  if (event->kind == QA_Q2W_EVENT_VIEW_WEAPON) {
    qa_ss_view_model model; memset(&model, 0, sizeof model);
    model.path = event->model; model.frame = event->frame;
    model.has_q2 = true; model.q2_skin = event->skin; model.q2_rate = event->rate;
    model.q2_kick_origin = event->kick_origin; model.q2_kick_angles = event->kick_angles;
    qa_ss_map_set(&sim->view_models, event->actor, &model);
  }
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2-weapon"; source.q2_weapon = event;
  qa_source_time time = sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2")
      && qa_ss_is(content, sim->weapon_provider.content) ? sim->selected_weapon_source.frame.time : sim->source_frame.time;
  qa_app_shared_sim_emit(sim, content, source, &time, NULL);
}

/* ---- syncSelectedQ1HealthLimit (4443-4448) ---- */
void qa_app_shared_sim_sync_selected_q1_health_limit(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!ss_source_is(sim, QA_SS_SOURCE_QUAKEC) || !ss_family_is(sim, "q1")) return;
  qa_q1_player_state *player = qa_q1_player(qa_app_shared_sim_selected_q1_game(sim), actor);
  if (player == NULL) ss_fail("Selected Q1 equipment has no current player");
  qa_app_sim_quakec_source_set_client_max_health(sim->source.quakec, actor, player->max_health);
}
/* ---- captureSelectedArsenalTravel (4423-4428): false = TS undefined. The
 * q1 state and the q2 inventory are malloc'd (qa_ss_selected_arsenal_travel_free). */
bool qa_app_shared_sim_capture_selected_arsenal_travel(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_selected_arsenal_travel *out) {
  memset(out, 0, sizeof *out);
  if (!sim->has_selected_arsenal) return false;
  if (sim->selected_arsenal.q1) {
    out->kind = "q1"; out->q1 = qa_app_q1_selected_arsenal_capture_travel(sim->selected_arsenal.q1, actor);
  } else if (sim->selected_arsenal.q2) {
    out->kind = "q2";
    qa_arsenal_state weapon = ss_sel_read(sim, actor);
    qa_arsenal_state inventory = ss_sel_read(sim, actor);
    out->q2_weapon = weapon.active_weapon; ss_arsenal_free(&weapon);
    out->q2_inventory = inventory.ammo; out->q2_inventory_count = inventory.ammo_count;
  } else {
    out->kind = "q3"; out->q3 = qa_app_q3_selected_arsenal_capture(sim->selected_arsenal.q3, actor);
    out->q3_milliseconds = sim->selected_milliseconds;
  }
  return true;
}
void qa_ss_selected_arsenal_travel_free(qa_ss_selected_arsenal_travel *travel) {
  if (qa_ss_is(travel->kind, "q1")) qa_app_q1_selected_arsenal_travel_destroy(&travel->q1);
  else if (qa_ss_is(travel->kind, "q2")) free((void *)travel->q2_inventory);
  memset(travel, 0, sizeof *travel);
}
/* ---- restoreSelectedArsenalTravel (4403-4421) ---- */
void qa_app_shared_sim_restore_selected_arsenal_travel(qa_app_shared_simulation *sim, qa_owned_actor actor, double maximum,
    const qa_ss_selected_arsenal_travel *carry) {
  if (ss_family_is(sim, "q1") && qa_ss_is(carry->kind, "q1")) {
    qa_arsenal_state admitted = qa_app_q1_selected_arsenal_admit_travel(sim->selected_arsenal.q1, actor, maximum, carry->q1);
    ss_arsenal_free(&admitted);
  } else if (ss_family_is(sim, "q2") && qa_ss_is(carry->kind, "q2") && sim->has_selected_weapon_source
             && qa_ss_is(sim->selected_weapon_source.kind, "q2")) {
    qa_arsenal_state admitted = qa_app_q2_selected_arsenal_admit(sim->selected_arsenal.q2, actor, maximum);
    ss_arsenal_free(&admitted);
    for (size_t i = 0; i < carry->q2_inventory_count; i++) {
      const qa_inventory_entry *entry = &carry->q2_inventory[i];
      const qa_inventory_entry *destination = ss_entry(sim, actor.id, entry->item);
      if (destination == NULL) ss_fail("Selected Q2 travel item has no admitted inventory entry");
      qa_inventory_entry configured = *destination;
      configured.count = qa_js_min(entry->count, destination->capacity);
      ss_configure(sim, actor, configured);
    }
    qa_q2w_weapons *weapons = sim->selected_weapon_source.weapons;
    const qa_q2w_definition *weapon = NULL;
    if (carry->q2_weapon != NULL) {
      size_t count = 0;
      const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(weapons, &count);
      for (size_t i = 0; i < count; i++) if (qa_ss_is(definitions[i]->item, carry->q2_weapon)) { weapon = definitions[i]; break; }
      if (weapon == NULL) ss_fail("Selected Q2 travel weapon is not registered");
    }
    qa_q2w_state *state = qa_q2w_ballistics_state(&weapons->base, actor.id);
    if (state == NULL) ss_fail("Selected Q2 travel has no native weapon state");
    qa_q2w_state_init(state, weapon ? weapon->name : NULL); /* Object.assign(state, new Q2WeaponState(...)) */
  } else if (ss_family_is(sim, "q3") && qa_ss_is(carry->kind, "q3")) {
    qa_app_q3_selected_arsenal_checkpoint state = carry->q3;
    if (state.has_last_fire_milliseconds)
      state.last_fire_milliseconds = state.last_fire_milliseconds + sim->selected_milliseconds - carry->q3_milliseconds;
    qa_app_q3_selected_arsenal_restore(sim->selected_arsenal.q3, actor, state);
  } else ss_fail("Campaign travel selected arsenal family differs from destination");
}

/* ---- arsenal (4729-4741) ---- */
qa_arsenal_state qa_app_shared_sim_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if (sim->has_selected_arsenal) return ss_sel_read(sim, player->actor.id);
  return sim->strategy->base_arsenal(sim, player);
}
/* Q1Strategy.baseArsenal / Q2Strategy.baseArsenal */
qa_arsenal_state qa_ss_q1_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  {
    qa_q1_player_state *state = qa_q1_player(sim->source.q1_game, player->actor.id);
    if (state == NULL) ss_fail("Q1 player has no arsenal");
    qa_arsenal_state arsenal; memset(&arsenal, 0, sizeof arsenal);
    arsenal.provider = sim->weapon_provider.provider;
    arsenal.active_weapon = qa_q1_services_weapon_item(sim->source.q1_game, state->weapon);
    arsenal.ammo = qa_shared_inventory_table_entries(sim->inventory, player->actor.id, &arsenal.ammo_count);
    arsenal.state.kind = "q1"; arsenal.state.frame = state->weapon_frame; arsenal.state.attack_finished_seconds = state->attack_finished;
    arsenal.state.source_weapon = qa_q1_is_base_weapon(state->weapon) ? qa_q1_weapon_bit(state->weapon) : 0;
    return arsenal;
  }
}
qa_arsenal_state qa_ss_q2_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_app_inventory_access inventory = qa_app_shared_sim_inventory_access(sim);
  return qa_app_project_q2_arsenal(player->actor.id, sim->weapon_provider.provider, sim->source.q2_weapons, inventory);
}
/* ---- arsenalState (7760-7762): false = TS null; the ammo array of a
 * selected read is malloc'd, a player's cached arsenal is borrowed. */
bool qa_app_shared_sim_arsenal_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_arsenal_state *out, bool *out_owned) {
  *out_owned = false;
  if (ss_sel_has(sim, actor)) { *out = ss_sel_read(sim, actor); *out_owned = true; return true; }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player == NULL) return false;
  *out = player->arsenal;
  return true;
}
/* ---- frameSelectedQ2Weapon (4807-4814) ---- */
void qa_app_shared_sim_frame_selected_q2_weapon(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (!ss_family_is(sim, "q2") || player == NULL || !ss_sel_has(sim, actor.id) || qa_ss_map_has(&sim->primary_command_blocks, &actor.id)) return;
  qa_app_q2_selected_arsenal_frame(sim->selected_arsenal.q2, actor.id);
  qa_app_shared_sim_store_player_arsenal(sim, player, ss_sel_read(sim, actor.id));
  ss_reconcile(sim, actor.id);
}
/* ---- playerWeapon (4816-4822) ---- */
void qa_app_shared_sim_player_weapon(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if (!player->has_cutscene) sim->strategy->player_weapon(sim, player);
}
/* Q1Strategy.playerWeapon */
void qa_ss_q1_player_weapon(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  bool pressed = ((int64_t)player->buttons & 1) != 0;
  qa_comp_q1_source_composition_impulse(sim->source.q1_composition, player->actor.id);
  if (!sim->has_selected_arsenal && qa_q1_weapon_input(sim->source.q1_game, player->actor, pressed, player->view_angles,
          qa_app_shared_sim_time_seconds(sim), player->water_level)) {
    qa_arsenal_state arsenal = qa_app_shared_sim_arsenal(sim, player);
    qa_comp_q1_source_composition_fired(sim->source.q1_composition, player->actor.id, arsenal.active_weapon);
  }
}
/* ---- projectQuakeCAttack (4886-4893) ---- */
void qa_app_shared_sim_project_quake_c_attack(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_arsenal_state before) {
  qa_arsenal_state after;
  if (!sim->strategy->attack_arsenal(sim, actor.id, &after)) return;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player != NULL && player->character == QA_GAME_FAMILY_Q2 && qa_ss_is(before.state.kind, "q1") && qa_ss_is(after.state.kind, "q1")
      && after.state.attack_finished_seconds > before.state.attack_finished_seconds && ss_health_or(sim, actor.id, 0) > 0)
    player->animation = qa_app_sim_quakec_character_animation(player->animation, "attack", player->bounds.max.z < player->standing_bounds.max.z);
}
/* ---- runQuakeWorldNewMissile (5041-5050) ---- */
void qa_app_shared_sim_run_quake_world_new_missile(qa_app_shared_simulation *sim) {
  if (!ss_source_is(sim, QA_SS_SOURCE_QUAKEC) || !qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "quakeworld")) return;
  qa_owned_actor actor;
  if (!qa_app_sim_quakec_source_take_new_missile(sim->source.quakec, &actor)) return;
  const qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (entry == NULL || entry->kind != QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) ss_fail("QW newmis has no shared source execution");
  qa_app_sim_actor_visited visited = {0};
  qa_frame_context frame = sim->source_frame; frame.elapsed.kind = QA_SOURCE_TIME_SECONDS; frame.elapsed.value = 0.05;
  qa_app_sim_actor_execution_frame context = {sim->actors, ss_bodies(sim), sim->physics, sim->scheduler, frame,
      qa_app_shared_sim_time_seconds(sim), 0.05, &visited};
  qa_app_sim_execute_actor(entry, &context);
  free(visited.actors);
  qa_app_sim_shared_physics_commit_attachments(sim->physics);
}

/* ---- nativeDroppedItem (6942-6951) ---- */
typedef struct ss_consume_call { qa_app_shared_simulation *sim; qa_owned_actor owner; qa_item_id item; double count; bool consumed; } ss_consume_call;
static void ss_consume_run(void *ctx) {
  ss_consume_call *call = ctx;
  call->consumed = qa_shared_inventory_table_consume(call->sim->inventory, call->owner, call->item, call->count);
}
bool qa_app_shared_sim_native_dropped_item(qa_app_shared_simulation *sim, qa_actor_id actor, qa_actor_id pickup, qa_item_id item, double count) {
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) ss_fail("Dropped item has no live inventory owner");
  qa_app_sim_dropped_pickup_cargo cargo = {item, count};
  qa_app_sim_source_pickup_cargo_set(sim->dropped_pickups, pickup, cargo);
  ss_consume_call call = {sim, owner, item, count, false};
  qa_caught_error caught;
  if (!qa_try(ss_consume_run, &call, &caught)) {
    qa_app_sim_source_pickup_cargo_delete(sim->dropped_pickups, pickup);
    qa_rethrow(&caught);
  }
  if (!call.consumed) { qa_app_sim_source_pickup_cargo_delete(sim->dropped_pickups, pickup); return false; }
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor);
  return true;
}
/* ---- restoreNativeInventoryCursors (6953-6960) ---- */
void qa_app_shared_sim_restore_native_inventory_cursors(qa_app_shared_simulation *sim, const qa_actor_id *actor) {
  for (size_t i = 0; i < sim->restored_native_inventory_selections.count;) {
    qa_actor_id owner = qa_ss_map_key_at(&sim->restored_native_inventory_selections, i)->actor;
    qa_ss_optional_item item = *(qa_ss_optional_item *)qa_ss_map_value_at(&sim->restored_native_inventory_selections, i);
    if (actor != NULL && !qa_same_actor(owner, *actor)) { i++; continue; }
    if (sim->native_primary_inventory == NULL) ss_fail("Saved native inventory cursor has no qualified owner");
    qa_compat_q2_native_primary_inventory_restore(sim->native_primary_inventory, owner, item.has ? item.item : NULL);
    qa_ss_map_delete(&sim->restored_native_inventory_selections, &owner); /* the next entry moves to index i */
  }
}

/* ---- q2-native source views ---- */
static qa_app_sim_classic_guest_services *ss_classic(qa_app_shared_simulation *sim) {
  return qa_app_sim_classic_guest_world_services(sim->source.classic_world);
}
static qa_app_sim_rerelease_guest_services *ss_rerelease(qa_app_shared_simulation *sim) {
  return qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world);
}
static bool ss_native_classic(const qa_app_shared_simulation *sim) { return qa_ss_is(sim->source.edition, "classic"); }
/* this.source.game.playerState(slot).movement.flags */
static double ss_native_movement_flags(qa_app_shared_simulation *sim, double slot) {
  if (ss_native_classic(sim)) return qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot).movement.flags;
  return qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot).movement.flags;
}
/* this.options.nativeQ2Travel?.clients.find(entry => entry.client.equals(client)) */
static const qa_app_sim_native_q2_travel_client *ss_native_travel_client(qa_app_shared_simulation *sim, qa_client_id client) {
  const qa_app_sim_native_q2_travel *travel = sim->options.native_q2_travel;
  if (travel == NULL) return NULL;
  const qa_app_sim_native_q2_travel_clients *base = travel->rerelease ? &travel->rerelease_travel.base : &travel->classic.base;
  for (size_t i = 0; i < base->client_count; i++) if (qa_same_client(base->clients[i].client, client)) return &base->clients[i];
  return NULL;
}
/* this.source.kind === "q3" ? this.source.game.records.nativeByActor(actor)?.client : null */
static qa_q3_game_client *ss_q3_native_client(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3)) return NULL;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  return entity ? entity->client : NULL;
}
/* this.source.game.records.byActor(actor)?.client */
static qa_q3_game_client *ss_q3_client(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3)) return NULL;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  return entity ? entity->client : NULL;
}
static double ss_q3_powerup(const qa_q3_game_client *client, double powerup) {
  return qa_q3_base_player_state_slots_get(&client->ps.powerups, powerup);
}

/* ---- bindEquipmentInventory (2845-2849) / bindSourceInventory (2851-2869) ---- */
#define SS_SOURCE_ITEMS 128
typedef struct ss_source_inventory {
  qa_app_shared_simulation *sim;
  qa_owned_actor actor;
  qa_inventory_state_binding source;
  qa_item_id replaced[SS_SOURCE_ITEMS]; size_t replaced_count; bool has_replaced;
  qa_item_id native_items[SS_SOURCE_ITEMS]; size_t native_count;
  qa_inventory_entry supplemental[SS_SOURCE_ITEMS]; size_t supplemental_count;
  qa_inventory_entry read[2 * SS_SOURCE_ITEMS];
  qa_inventory_entry original[SS_SOURCE_ITEMS];
} ss_source_inventory;
static bool ss_item_in(const qa_item_id *items, size_t count, qa_item_id item) {
  for (size_t i = 0; i < count; i++) if (qa_ss_is(items[i], item)) return true;
  return false;
}
/* original(): source.read() without the replaced items. */
static size_t ss_source_original(ss_source_inventory *binding) {
  size_t count = 0, kept = 0;
  const qa_inventory_entry *entries = binding->source.read(binding->source.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    if (binding->has_replaced && ss_item_in(binding->replaced, binding->replaced_count, entries[i].item)) continue;
    if (kept == SS_SOURCE_ITEMS) qa_range_error("Source inventory exceeds the bound row capacity");
    binding->original[kept++] = entries[i];
  }
  return kept;
}
/* capacity(item): undefined unless a QuakeC program carries a selected Q1 arsenal. */
static bool ss_source_capacity(ss_source_inventory *binding, qa_item_id item, double *out) {
  qa_app_shared_simulation *sim = binding->sim;
  if (sim->options.prepared_quakec == NULL || !sim->has_selected_weapon_source || !qa_ss_is(sim->selected_weapon_source.kind, "q1")) return false;
  return qa_q1_inventory_capacity(sim->selected_weapon_source.q1_game, binding->actor.id, item, out);
}
static const qa_inventory_entry *ss_source_read(void *ctx, size_t *out_count) {
  ss_source_inventory *binding = ctx;
  size_t count = ss_source_original(binding), total = 0;
  for (size_t i = 0; i < count; i++) {
    qa_inventory_entry entry = binding->original[i]; double capacity;
    if (ss_source_capacity(binding, entry.item, &capacity)) entry.capacity = capacity;
    binding->read[total++] = entry;
  }
  for (size_t i = 0; i < binding->supplemental_count; i++) binding->read[total++] = binding->supplemental[i];
  *out_count = total;
  return binding->read;
}
static bool ss_source_mutable_capacity(void *ctx, qa_item_id item) {
  ss_source_inventory *binding = ctx; double capacity;
  if (ss_item_in(binding->native_items, binding->native_count, item))
    return ss_source_capacity(binding, item, &capacity)
        || (binding->source.mutable_capacity != NULL && binding->source.mutable_capacity(binding->source.ctx, item));
  for (size_t i = 0; i < binding->supplemental_count; i++) if (qa_ss_is(binding->supplemental[i].item, item)) return true;
  return false;
}
static void ss_source_write(void *ctx, qa_inventory_entry entry) {
  ss_source_inventory *binding = ctx;
  if (ss_item_in(binding->native_items, binding->native_count, entry.item)) {
    double selected;
    if (ss_source_capacity(binding, entry.item, &selected) && entry.capacity != selected)
      ss_fail("Inventory capacity differs from its selected source owner");
    binding->source.write(binding->source.ctx, entry);
    return;
  }
  for (size_t i = 0; i < binding->supplemental_count; i++)
    if (qa_ss_is(binding->supplemental[i].item, entry.item)) { binding->supplemental[i] = entry; return; }
  if (binding->supplemental_count == SS_SOURCE_ITEMS) qa_range_error("Supplemental inventory exceeds the bound row capacity");
  binding->supplemental[binding->supplemental_count++] = entry;
}
void qa_app_shared_sim_bind_source_inventory(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_inventory_state_binding source,
    const qa_inventory_entry *saved, size_t saved_count) {
  for (size_t i = 0; i < saved_count; i++) for (size_t j = 0; j < i; j++)
    if (qa_ss_is(saved[i].item, saved[j].item)) ss_fail("Saved source inventory has duplicate items");
  ss_source_inventory *binding = qa_app_shared_sim_keep(sim, sizeof *binding);
  binding->sim = sim; binding->actor = actor; binding->source = source;
  if (ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE) && sim->selected_original_supply != NULL) {
    size_t count = 0;
    qa_item_id *replaced = qa_ss_replaced_supply_items(sim->selected_original_supply, &count);
    if (count + 1 > SS_SOURCE_ITEMS) { free(replaced); qa_range_error("Replaced inventory exceeds the bound row capacity"); }
    for (size_t i = 0; i < count; i++) binding->replaced[binding->replaced_count++] = replaced[i];
    binding->replaced[binding->replaced_count++] = "q2:weapon_blaster";
    binding->has_replaced = true;
    free(replaced);
  }
  size_t original = ss_source_original(binding);
  for (size_t i = 0; i < original; i++) binding->native_items[binding->native_count++] = binding->original[i].item;
  for (size_t i = 0; i < saved_count; i++) {
    if (ss_item_in(binding->native_items, binding->native_count, saved[i].item)) continue;
    if (binding->supplemental_count == SS_SOURCE_ITEMS) qa_range_error("Supplemental inventory exceeds the bound row capacity");
    binding->supplemental[binding->supplemental_count++] = saved[i];
  }
  qa_inventory_state_binding bound = {binding, ss_source_read, NULL, ss_source_write, ss_source_mutable_capacity};
  qa_shared_inventory_table_bind(sim->inventory, actor, bound);
}
void qa_app_shared_sim_bind_equipment_inventory(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_inventory_entry *initial,
    size_t initial_count) {
  qa_inventory_state_binding source;
  if (!sim->strategy->source_inventory(sim, actor.id, &source)) { qa_shared_inventory_table_create_entries(sim->inventory, actor, initial, initial_count); return; }
  qa_app_shared_sim_bind_source_inventory(sim, actor, source, initial, initial_count);
}
/* Q2NativeStrategy.sourceInventory */
bool qa_ss_q2_native_source_inventory(qa_app_shared_simulation *sim, qa_actor_id actor, qa_inventory_state_binding *out) {
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  return ss_native_classic(sim) ? qa_app_sim_classic_guest_services_source_inventory(ss_classic(sim), slot, out)
      : qa_app_sim_rerelease_guest_services_source_inventory(ss_rerelease(sim), slot, out);
}
/* Q2NativeStrategy.teleportBits / Q3QvmStrategy.teleportBits */
bool qa_ss_q2_native_teleport_bits(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) {
  *out = (double)(qa_js_to_int32(ss_native_movement_flags(sim, qa_app_shared_sim_native_q2_client(sim, actor).slot + 1)) & 32);
  return true;
}
bool qa_ss_q3_qvm_teleport_bits(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) {
  ss_qvm_state state; ss_qvm_actor_state(sim, actor, &state);
  *out = (double)(qa_js_to_int32(state.state.flags) & 4);
  return true;
}

/* ---- equipmentPlayerAvailable (2871-2879) ---- */
bool qa_app_shared_sim_equipment_player_available(qa_app_shared_simulation *sim, qa_actor_id actor) {
  bool available;
  if (sim->strategy->player_available(sim, actor, &available)) return available;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  return player != NULL && !player->intermission && !player->has_cutscene && ss_health_or(sim, actor, 0) > 0;
}
/* Q3QvmStrategy.playerAvailable */
bool qa_ss_q3_qvm_player_available(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  qa_app_q3_player player;
  if (!ss_qvm_player_of(sim, actor, &player)) { *out = false; return true; }
  ss_qvm_state state; ss_qvm_state_read(sim, player.source_entity, &state);
  if (sim->source.qvm_weapons != NULL) *out = qa_qvm_primary_weapons_available(sim->source.qvm_weapons, actor, false);
  else *out = state.stats[0] > 0 && state.state.movement_type != QA_Q3_PM_SPECTATOR && state.state.movement_type != QA_Q3_PM_INTERMISSION
      && state.state.movement_type != QA_Q3_PM_SPINTERMISSION;
  return true;
}
/* Q2NativeStrategy.playerAvailable: host.playerClient(actor) !== null && host.nativeAvailable(actor) */
bool qa_ss_q2_native_player_available(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  qa_client_id client;
  *out = qa_app_shared_sim_player_client(sim, actor, &client) && qa_app_shared_sim_native_available(sim, actor);
  return true;
}
/* TS host.nativeAvailable: nativePrimaryWeapons?.available(actor) ?? strategy.guestUi(actor).health > 0 */
bool qa_app_shared_sim_native_available(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->native_primary_weapons != NULL) return qa_compat_q2_native_primary_weapons_available(sim->native_primary_weapons, actor);
  return sim->strategy->guest_ui(sim, actor).health > 0;
}

/* ---- primaryHandoff (2933-2978) ---- */
typedef struct ss_handoff {
  qa_app_shared_simulation *sim; qa_actor_id actor; qa_app_sim_movement_player *player;
  bool holstered;
  qa_q2_entity *entity;          /* q2 */
  qa_q1_services *q1;            /* q1 */
  qa_owned_actor owner;
} ss_handoff;
/* native: accepts(item) */
static bool ss_native_accepts(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  qa_app_sim_player_ui ui = qa_app_shared_sim_native_player_ui(handoff->sim, handoff->actor);
  if (ui.active_weapon != NULL && qa_ss_is(ui.active_weapon, item)) return true;
  for (size_t i = 0; i < ui.item_count; i++)
    if (qa_ss_is(ui.items[i].id, item) && qa_ss_is(ui.items[i].kind, "weapon") && ui.items[i].owned) return true;
  return false;
}
static bool ss_native_select(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx; qa_app_shared_simulation *sim = handoff->sim;
  if (!ss_native_accepts(ctx, item)) return false;
  qa_app_sim_player_ui ui = qa_app_shared_sim_native_player_ui(sim, handoff->actor);
  if (ui.active_weapon != NULL && qa_ss_is(ui.active_weapon, item)) return true;
  if (ss_source_is(sim, QA_SS_SOURCE_Q3_QVM)) {
    const qa_app_sim_player_ui_item *weapon = NULL;
    for (size_t i = 0; i < ui.item_count; i++) if (qa_ss_is(ui.items[i].id, item) && qa_ss_is(ui.items[i].kind, "weapon")) { weapon = &ui.items[i]; break; }
    if (weapon == NULL) return false;
    qa_ss_map_set(&sim->native_weapon_requests, &handoff->actor, &weapon->source_ordinal);
  } else {
    const char *name = NULL;
    for (size_t i = 0; i < ui.item_count; i++) if (qa_ss_is(ui.items[i].id, item)) { name = ui.items[i].label; break; }
    if (name == NULL) return false;
    double slot = qa_app_shared_sim_native_q2_client(sim, handoff->actor).slot + 1;
    const char *arguments[2] = {"use", name};
    if (ss_native_classic(sim)) qa_app_sim_classic_guest_world_command(sim->source.classic_world, slot, arguments, 2, name);
    else qa_app_sim_rerelease_guest_world_command(sim->source.rerelease_world, slot, arguments, 2, name);
  }
  return true;
}
static void ss_native_holster(void *ctx) { ((ss_handoff *)ctx)->holstered = true; }
static bool ss_native_is_holstered(void *ctx) { return ((ss_handoff *)ctx)->holstered; }
static bool ss_native_resume(void *ctx, qa_item_id item) {
  ((ss_handoff *)ctx)->holstered = false;
  return item == NULL || ss_native_select(ctx, item);
}
/* quakec */
static bool ss_qc_accepts(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  qa_app_sim_player_ui ui = qa_app_shared_sim_primary_ui(handoff->sim, handoff->actor);
  for (size_t i = 0; i < ui.item_count; i++)
    if (qa_ss_is(ui.items[i].id, item) && qa_ss_is(ui.items[i].kind, "weapon") && ui.items[i].owned) return true;
  return false;
}
static bool ss_qc_select(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  return qa_app_sim_quakec_source_request_client_weapon(handoff->sim->source.quakec, handoff->actor, item);
}
static void ss_qc_holster(void *ctx) {
  ss_handoff *handoff = ctx;
  if (!qa_session_actor_registry_is_live(handoff->sim->actors, handoff->actor)) ss_fail("QC weapon owner retired");
}
static bool ss_qc_is_holstered(void *ctx) {
  ss_handoff *handoff = ctx;
  return qa_app_sim_quakec_source_client_weapon_settled(handoff->sim->source.quakec, handoff->actor);
}
static bool ss_qc_resume(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  return item == NULL || qa_app_sim_quakec_source_request_client_weapon(handoff->sim->source.quakec, handoff->actor, item);
}
/* q1: Q1EntityServices.primaryWeaponHandoff(actor) (content/q1/foundation/
 * entity-services.ts 741-768), which the C Q1 foundation does not export. */
static bool ss_q1_resolve(ss_handoff *handoff, qa_item_id item, qa_q1_weapon *out) {
  qa_q1_services *game = handoff->q1;
  qa_q1_player_state *player = qa_q1_player(game, handoff->actor);
  for (size_t i = 0; i < 8 + game->registered_weapon_count; i++) {
    qa_q1_weapon candidate = i < 8 ? QA_Q1_WEAPONS[i] : game->registered_weapons[i - 8].def->id;
    if (!qa_ss_is(qa_q1_services_weapon_item(game, candidate), item)) continue;
    if (!qa_q1_weapon_available(game, player, candidate, "best")) return false;
    *out = candidate;
    return true;
  }
  return false;
}
static bool ss_q1_accepts(void *ctx, qa_item_id item) { qa_q1_weapon weapon; return ss_q1_resolve(ctx, item, &weapon); }
static bool ss_q1_select(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx; qa_q1_weapon weapon;
  return ss_q1_resolve(handoff, item, &weapon) && qa_q1_select_weapon(handoff->q1, handoff->owner, weapon);
}
static void ss_q1_holster(void *ctx) {
  ss_handoff *handoff = ctx;
  qa_q1_player_state *player = qa_q1_player(handoff->q1, handoff->actor);
  if (player->primary_holstered) return;
  player->primary_holstered = true;
  player->weapon_frame = 0; player->continuous_firing = false; player->weapon_animation_at = -1;
}
static bool ss_q1_is_holstered(void *ctx) {
  ss_handoff *handoff = ctx;
  return qa_q1_player(handoff->q1, handoff->actor)->primary_holstered;
}
static bool ss_q1_resume(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  qa_q1_player_state *player = qa_q1_player(handoff->q1, handoff->actor);
  qa_q1_weapon requested = NULL, weapon;
  if (item == NULL) requested = player->weapon;
  else if (!ss_q1_resolve(handoff, item, &requested)) requested = NULL;
  weapon = requested != NULL && qa_q1_weapon_available(handoff->q1, player, requested, "best") ? requested : qa_q1_best_weapon(handoff->q1, handoff->owner);
  qa_q1_select_weapon(handoff->q1, handoff->owner, weapon);
  player->primary_holstered = false;
  return item == NULL || qa_ss_is(qa_q1_services_weapon_item(handoff->q1, player->weapon), item);
}
/* q2 */
static const qa_q2w_definition *ss_q2_definition(qa_q2w_weapons *weapons, qa_item_id item) {
  size_t count = 0;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(weapons, &count);
  for (size_t i = 0; i < count; i++) if (qa_ss_is(definitions[i]->item, item)) return definitions[i];
  return NULL;
}
/* TS passes the Q2Entity, whose actor and viewHeight are the Q2WeaponOwner. */
static qa_q2w_owner ss_q2_owner(qa_q2_entity *entity) { qa_q2w_owner owner = {entity->actor, entity->view_height}; return owner; }
static bool ss_q2_accepts(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  return ss_q2_definition(handoff->sim->source.q2_weapons, item) != NULL && ss_count(handoff->sim, handoff->actor, item) > 0;
}
static bool ss_q2_select(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx; qa_app_shared_simulation *sim = handoff->sim;
  const qa_q2w_definition *weapon = ss_q2_definition(sim->source.q2_weapons, item);
  if (weapon == NULL) return false;
  qa_q2w_selection result = qa_q2w_weapons_request_weapon(sim->source.q2_weapons, ss_q2_owner(handoff->entity),
      sim->source.product->services, weapon->name, false);
  return result == QA_Q2W_SEL_SELECTED || result == QA_Q2W_SEL_CURRENT;
}
static void ss_q2_holster(void *ctx) {
  ss_handoff *handoff = ctx;
  qa_q2w_weapons_request_holster(handoff->sim->source.q2_weapons, ss_q2_owner(handoff->entity));
}
static bool ss_q2_is_holstered(void *ctx) {
  ss_handoff *handoff = ctx;
  return qa_q2w_weapons_is_holstered(handoff->sim->source.q2_weapons, ss_q2_owner(handoff->entity));
}
static bool ss_q2_resume(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx; qa_app_shared_simulation *sim = handoff->sim;
  const qa_q2w_definition *definition = item == NULL ? NULL : ss_q2_definition(sim->source.q2_weapons, item);
  qa_q2w_weapons_resume_primary(sim->source.q2_weapons, ss_q2_owner(handoff->entity), sim->source.product->services,
      qa_app_shared_sim_q2_weapon_input(sim, handoff->player), definition ? definition->name : NULL);
  if (item == NULL) return true;
  qa_arsenal_state arsenal = qa_app_shared_sim_arsenal(sim, handoff->player);
  return arsenal.active_weapon != NULL && qa_ss_is(arsenal.active_weapon, item);
}
/* q3 */
static qa_q3_arsenal_runtime_state *ss_q3_runtime(ss_handoff *handoff) {
  qa_q3_arsenal_runtime_state *state = qa_ss_map_get(&handoff->sim->q3_arsenals, &handoff->player->actor);
  if (state == NULL) ss_fail("Missing Q3 primary continuation");
  return state;
}
static const qa_q3_weapon_item *ss_q3_weapon_for_item(qa_item_id item) {
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) if (qa_ss_is(qa_q3_weapon_items[i].item, item)) return &qa_q3_weapon_items[i];
  return NULL;
}
static bool ss_q3_select(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  const qa_q3_weapon_item *weapon = ss_q3_weapon_for_item(item);
  if (weapon == NULL || ss_count(handoff->sim, handoff->actor, item) <= 0
      || (qa_ss_is(ss_q3_runtime(handoff)->product, "baseq3") && weapon->weapon > 10)) return false;
  qa_q3_arsenal_runtime_state next = qa_q3_request_weapon(*ss_q3_runtime(handoff), weapon->weapon);
  qa_ss_map_set(&handoff->sim->q3_arsenals, &handoff->player->actor, &next);
  return true;
}
static bool ss_q3_accepts(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  bool known = false;
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++)
    if (qa_ss_is(qa_q3_weapon_items[i].item, item) && (qa_ss_is(ss_q3_runtime(handoff)->product, "missionpack") || qa_q3_weapon_items[i].weapon <= 10)) { known = true; break; }
  return known && ss_count(handoff->sim, handoff->actor, item) > 0;
}
static void ss_q3_holster(void *ctx) {
  ss_handoff *handoff = ctx;
  qa_q3_arsenal_runtime_state next = qa_q3_request_weapon_holster(*ss_q3_runtime(handoff));
  qa_ss_map_set(&handoff->sim->q3_arsenals, &handoff->player->actor, &next);
}
static bool ss_q3_is_holstered(void *ctx) { return ss_q3_runtime(ctx)->external_slot == QA_Q3_WEAPON_SLOT_HOLSTERED; }
static bool ss_q3_resume(void *ctx, qa_item_id item) {
  ss_handoff *handoff = ctx;
  qa_q3_arsenal_runtime_state next = qa_q3_request_weapon_resume(*ss_q3_runtime(handoff));
  qa_ss_map_set(&handoff->sim->q3_arsenals, &handoff->player->actor, &next);
  return item == NULL || ss_q3_select(ctx, item);
}
/* An immediate PrimaryWeaponHandoff as the weapon slot's SourceWeaponHandoff. */
static qa_source_weapon_handoff ss_source_handoff(qa_app_primary_weapon_handoff primary) {
  qa_source_weapon_handoff handoff; memset(&handoff, 0, sizeof handoff);
  handoff.ctx = primary.ctx; handoff.provider = primary.provider; handoff.accepts = primary.accepts; handoff.select = primary.select;
  handoff.holster = primary.holster; handoff.is_holstered = primary.is_holstered; handoff.is_source_input = false;
  handoff.resume_immediate = primary.resume;
  return handoff;
}
/* TS primaryHandoff: the selected arsenal's handoff, else this.strategy.handoff(actor). */
qa_app_primary_weapon_handoff qa_app_shared_sim_primary_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->has_selected_arsenal) return ss_sel_handoff(sim, actor);
  return sim->strategy->handoff(sim, actor);
}
static qa_app_primary_weapon_handoff ss_handoff_begin(qa_app_shared_simulation *sim, qa_actor_id actor, ss_handoff **out) {
  qa_app_primary_weapon_handoff handoff; memset(&handoff, 0, sizeof handoff);
  handoff.kind = "immediate"; handoff.provider = sim->weapon_provider.provider;
  ss_handoff *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->actor = actor; handoff.ctx = record;
  *out = record;
  return handoff;
}
/* SourceStrategy handoff members. */
qa_app_primary_weapon_handoff qa_ss_loaded_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor;
  ss_fail("Primary source is not ready");
}
qa_app_primary_weapon_handoff qa_ss_native_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_handoff *record;
  qa_app_primary_weapon_handoff handoff = ss_handoff_begin(sim, actor, &record);
  handoff.accepts = ss_native_accepts; handoff.select = ss_native_select; handoff.holster = ss_native_holster;
  handoff.is_holstered = ss_native_is_holstered; handoff.resume = ss_native_resume;
  return handoff;
}
qa_app_primary_weapon_handoff qa_ss_quakec_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_handoff *record;
  qa_app_primary_weapon_handoff handoff = ss_handoff_begin(sim, actor, &record);
  record->player = qa_app_shared_sim_require_player(sim, actor);
  handoff.accepts = ss_qc_accepts; handoff.select = ss_qc_select; handoff.holster = ss_qc_holster;
  handoff.is_holstered = ss_qc_is_holstered; handoff.resume = ss_qc_resume;
  return handoff;
}
qa_app_primary_weapon_handoff qa_ss_q1_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_handoff *record;
  qa_app_primary_weapon_handoff handoff = ss_handoff_begin(sim, actor, &record);
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  record->player = player;
  {
    if (qa_q1_player(sim->source.q1_game, actor) == NULL) ss_fail("Player has no Q1 weapon state");
    record->q1 = sim->source.q1_game; record->owner = player->actor;
    handoff.provider = qa_q1_services_provider(sim->source.q1_game);
    handoff.accepts = ss_q1_accepts; handoff.select = ss_q1_select; handoff.holster = ss_q1_holster;
    handoff.is_holstered = ss_q1_is_holstered; handoff.resume = ss_q1_resume;
    return handoff;
  }
}
qa_app_primary_weapon_handoff qa_ss_q2_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_handoff *record;
  qa_app_primary_weapon_handoff handoff = ss_handoff_begin(sim, actor, &record);
  record->player = qa_app_shared_sim_require_player(sim, actor);
  {
    qa_q2_game_services *game = sim->source.product->services;
    record->entity = game->entity(game->ctx, &actor);
    if (record->entity == NULL) ss_fail("Q2 primary has no source player");
    handoff.accepts = ss_q2_accepts; handoff.select = ss_q2_select; handoff.holster = ss_q2_holster;
    handoff.is_holstered = ss_q2_is_holstered; handoff.resume = ss_q2_resume;
    return handoff;
  }
}
qa_app_primary_weapon_handoff qa_ss_q3_handoff(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_handoff *record;
  qa_app_primary_weapon_handoff handoff = ss_handoff_begin(sim, actor, &record);
  record->player = qa_app_shared_sim_require_player(sim, actor);
  handoff.accepts = ss_q3_accepts; handoff.select = ss_q3_select; handoff.holster = ss_q3_holster;
  handoff.is_holstered = ss_q3_is_holstered; handoff.resume = ss_q3_resume;
  return handoff;
}

/* ---- bindWeaponSlot (2990-2995) / hasWeaponSlot (2996) ---- */
typedef struct ss_slot_owner { qa_app_shared_simulation *sim; qa_owned_actor owner; } ss_slot_owner;
static bool ss_slot_actor_current(void *ctx) {
  ss_slot_owner *record = ctx; qa_owned_actor current;
  return ss_owned(record->sim, record->owner.id, &current) && qa_same_owned_actor(current, record->owner);
}
void qa_app_shared_sim_bind_weapon_slot_state(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_weapon_slot_restore_state *state) {
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) ss_fail("Weapon slot actor retired");
  qa_source_weapon_handoff primary = ss_source_handoff(qa_app_shared_sim_primary_handoff(sim, actor));
  qa_app_sim_equipment_weapon_handoff equipment; bool has_equipment = false;
  if (sim->grapple != NULL && qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->binding, "slot")) {
    equipment = qa_app_sim_grapple_runtime_handoff(sim->grapple, actor); has_equipment = true;
  }
  ss_slot_owner *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->owner = owner;
  qa_app_sim_weapon_slot *slot = qa_app_sim_weapon_slot_create(primary, has_equipment ? &equipment : NULL, state, ss_slot_actor_current, record);
  qa_app_sim_weapon_slot *previous = ss_slot(sim, actor);
  qa_ss_map_set(&sim->weapon_slots, &actor, &slot);
  if (previous != NULL) qa_app_sim_weapon_slot_destroy(previous);
}
void qa_app_shared_sim_bind_weapon_slot(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_shared_sim_bind_weapon_slot_state(sim, actor, NULL);
}
bool qa_app_shared_sim_has_weapon_slot(qa_app_shared_simulation *sim, qa_actor_id actor) { return qa_ss_map_has(&sim->weapon_slots, &actor); }

/* ---- admitGrapple (2980-2989) ---- */
void qa_app_shared_sim_admit_grapple(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (sim->grapple != NULL) qa_app_sim_grapple_runtime_admit(sim->grapple, actor);
  if (sim->grapple != NULL && qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->binding, "slot")) {
    qa_owned_actor owner;
    if (!ss_owned(sim, actor, &owner)) ss_fail("Grapple owner was released");
    if (!qa_shared_inventory_table_has(sim->inventory, actor)) qa_app_shared_sim_bind_equipment_inventory(sim, owner, NULL, 0);
    qa_inventory_entry entry; memset(&entry, 0, sizeof entry);
    entry.item = qa_app_sim_grapple_runtime_weapon(sim->grapple).item; entry.count = 1; entry.capacity = 1;
    ss_configure(sim, owner, entry);
    if (!qa_ss_map_has(&sim->weapon_slots, &actor)) qa_app_shared_sim_bind_weapon_slot(sim, actor);
  }
}

/* ---- requireEquipmentPlayer (2829-2843) ---- */
bool qa_app_shared_sim_require_equipment_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!sim->strategy->owns_equipment_players(sim)) {
    qa_app_shared_sim_require_player(sim, actor);
    return true;
  }
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) ss_fail("Equipment owner is not an admitted source player");
  if ((sim->grapple != NULL || sim->hand_grenades != NULL) && !qa_ss_map_has(&sim->native_equipment_players, &actor)) {
    qa_owned_actor owner;
    if (!ss_owned(sim, actor, &owner)) ss_fail("Equipment owner was released");
    if (!qa_shared_inventory_table_has(sim->inventory, actor)) qa_app_shared_sim_bind_equipment_inventory(sim, owner, NULL, 0);
    const qa_app_sim_native_q2_travel_client *carry = ss_native_travel_client(sim, client);
    if (sim->hand_grenades != NULL) qa_app_sim_hand_grenade_runtime_admit(sim->hand_grenades, actor, carry ? carry->hand_grenades : NULL);
    qa_app_shared_sim_admit_grapple(sim, actor);
    qa_ss_map_set(&sim->native_equipment_players, &actor, NULL);
    bool available = qa_app_shared_sim_equipment_player_available(sim, actor);
    qa_ss_map_set(&sim->native_equipment_alive, &actor, &available);
    if (carry != NULL && carry->weapon_slot != NULL && qa_ss_map_has(&sim->weapon_slots, &actor)) {
      qa_app_primary_weapon_handoff primary = qa_app_shared_sim_primary_handoff(sim, actor);
      primary.holster(primary.ctx);
      qa_app_sim_weapon_slot_restore_state state; memset(&state, 0, sizeof state);
      state.kind = "holstering-primary"; state.next = *carry->weapon_slot;
      qa_app_shared_sim_bind_weapon_slot_state(sim, actor, &state);
      ss_reconcile(sim, actor);
    }
  }
  return true;
}

/* ---- setHandGrenadeInput (2824-2828) / handGrenadeState (2830) ---- */
void qa_app_shared_sim_set_hand_grenade_input(qa_app_shared_simulation *sim, qa_actor_id actor, bool held) {
  qa_app_shared_sim_assert_open(sim); qa_app_shared_sim_require_equipment_player(sim, actor);
  if (sim->hand_grenades == NULL) ss_fail("Hand grenades are disabled in this recipe");
  qa_app_sim_hand_grenade_runtime_input(sim->hand_grenades, actor, held);
}
bool qa_app_shared_sim_hand_grenade_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_hand_grenade_equipment_state *out) {
  if (sim->hand_grenades == NULL) return false;
  return qa_q2_hand_grenade_equipment_state_of(qa_app_sim_hand_grenade_runtime_controller(sim->hand_grenades), actor, out);
}
/* ---- setGrappleInput (2915-2919) / grappleState (2921-2924) / sharedGrapple (2926-2931) ---- */
void qa_app_shared_sim_set_grapple_input(qa_app_shared_simulation *sim, qa_actor_id actor, bool held) {
  qa_app_shared_sim_assert_open(sim); qa_app_shared_sim_require_equipment_player(sim, actor);
  if (sim->grapple == NULL || !qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->binding, "offhand"))
    ss_fail("No selected offhand grapple is available");
  qa_app_sim_grapple_runtime_input(sim->grapple, actor, held);
}
bool qa_app_shared_sim_grapple_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_grapple_state *out) {
  if (sim->grapple == NULL) return false;
  out->mechanic = qa_app_sim_grapple_runtime_selection(sim->grapple)->mechanic;
  out->has_hook = qa_app_sim_grapple_runtime_hook(sim->grapple, actor, &out->hook);
  out->pulling = qa_app_sim_grapple_runtime_pulling(sim->grapple, actor);
  out->gravity_scale = qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, actor);
  out->prediction_suppressed = qa_app_sim_grapple_runtime_prediction(sim->grapple, actor);
  return true;
}
static bool ss_grapple_native_slot(void *ctx, const char *mechanic) { (void)ctx; (void)mechanic; return false; }
static void ss_grapple_input(void *ctx, qa_actor_id actor, bool held) { qa_app_shared_sim_set_grapple_input(ctx, actor, held); }
static void ss_grapple_release(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ctx;
  if (sim->grapple != NULL) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
}
static bool ss_grapple_pulling(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ctx;
  return sim->grapple != NULL ? qa_app_sim_grapple_runtime_pulling(sim->grapple, actor) : false;
}
static int64_t ss_grapple_gravity_scale(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ctx;
  return sim->grapple != NULL ? (int64_t)qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, actor) : 1;
}
const qa_shared_grapple_control *qa_app_shared_sim_shared_grapple(qa_app_shared_simulation *sim) {
  qa_shared_grapple_control *control = qa_app_shared_sim_keep(sim, sizeof *control);
  control->ctx = sim; control->selection = sim->recipe.equipment.grapple;
  control->native_slot = ss_grapple_native_slot; control->input = ss_grapple_input; control->release = ss_grapple_release;
  control->pulling = ss_grapple_pulling; control->gravity_scale = ss_grapple_gravity_scale;
  return control;
}

/* ---- requestWeapon (2997-3003) ---- */
bool qa_app_shared_sim_request_weapon(qa_app_shared_simulation *sim, qa_actor_id actor, qa_weapon_reference weapon) {
  qa_app_shared_sim_assert_open(sim); qa_app_shared_sim_require_equipment_player(sim, actor);
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  if (slot != NULL) { bool accepted = qa_app_sim_weapon_slot_request(slot, weapon); qa_app_sim_weapon_slot_reconcile(slot); return accepted; }
  qa_app_primary_weapon_handoff primary = qa_app_shared_sim_primary_handoff(sim, actor);
  return qa_ss_is(weapon.provider, primary.provider) && primary.select(primary.ctx, weapon.item);
}

/* ---- grappleAnchor (3005-3014): "world" | "none" | "player" | "corpse" | "brush" | "box" ---- */
const char *qa_app_shared_sim_grapple_anchor(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_actor_id world;
  if (qa_app_shared_sim_world_actor(sim, &world) && qa_same_actor(world, actor)) return "world";
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) return "none";
  qa_app_sim_shared_solid collision; bool has_collision = qa_app_sim_shared_physics_solid_of_actor(sim->physics, actor, &collision);
  qa_spatial_actor linked; const qa_actor_collision *native = qa_shared_linked_actor(sim->scene, actor, &linked) ? &linked.collision : NULL;
  if ((!has_collision || qa_ss_is(collision.solid, "none")) && (native == NULL || !qa_ss_is(native->role, "solid"))) return "none";
  qa_client_id client;
  if (qa_app_shared_sim_player_client(sim, actor, &client)) return qa_app_shared_sim_equipment_player_available(sim, actor) ? "player" : "corpse";
  if (qa_ss_is(qa_app_shared_sim_classname(sim, actor), "bodyque")) return "corpse";
  if ((has_collision && qa_ss_is(collision.solid, "brush")) || (native != NULL && qa_ss_is(native->shape_kind, "model"))) return "brush";
  if ((has_collision && qa_ss_is(collision.solid, "box")) || (native != NULL && qa_ss_is(native->shape_kind, "box"))) return "box";
  return "none";
}

/* ---- q1CharacterPose (3016-3020) ---- */
qa_q1_character_source_pose qa_app_shared_sim_q1_character_pose(qa_app_shared_simulation *sim, qa_actor_id actor) {
  const qa_app_sim_grapple_source *source = sim->grapple ? qa_app_sim_grapple_runtime_source(sim->grapple) : NULL;
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  qa_q1_character_source_pose pose; memset(&pose, 0, sizeof pose);
  if (source != NULL && qa_ss_is(source->kind, "q1-threewave") && slot != NULL && qa_app_sim_weapon_slot_equipment_selected(slot)) {
    bool axe = false;
    qa_q1_threewave_character_pose(source->q1_core, actor, &axe, &pose.has_frame, &pose.frame);
    pose.has_axe_pose = true; pose.axe_pose = axe;
    return pose;
  }
  if (sim->strategy->character_pose(sim, actor, &pose)) return pose;
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(sim, actor);
  pose.has_axe_pose = true; pose.axe_pose = ui.active_weapon != NULL && qa_ss_is(ui.active_weapon, "q1:weapon/axe");
  return pose;
}

/* ---- q2WeaponInput (4824-4837) ---- */
qa_q2w_input qa_app_shared_sim_q2_weapon_input(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_actor_id actor = player->actor.id;
  bool selected = ss_family_is(sim, "q2");
  bool pressed = ((int64_t)player->buttons & 1) != 0 && (!selected || ss_primary_selected(sim, actor));
  qa_app_source_weapon_facts facts = sim->strategy->weapon_facts(sim, actor);
  qa_q3_game_client *q3 = facts.q3_client;
  qa_q2w_input input; memset(&input, 0, sizeof input);
  input.attack = pressed; input.latched_attack = pressed && ((int64_t)player->previous_buttons & 1) == 0; input.holster = false;
  input.angles = player->view_angles;
  input.ducked = player->bounds.max.z < player->standing_bounds.max.z;
  input.spectator = facts.spectator
      || (selected && (player->intermission || player->has_cutscene
          || (q3 != NULL && (q3->ps.pm_type == QA_Q3_PM_SPECTATOR || ((int64_t)q3->ps.pm_flags & QA_Q3_FLAG_RESPAWNED) != 0))));
  input.notarget = selected && qa_app_shared_sim_monster_target_notarget(sim, actor);
  input.hand = qa_ss_is(facts.hand, "left") ? QA_Q2W_HAND_LEFT : qa_ss_is(facts.hand, "center") ? QA_Q2W_HAND_CENTER : QA_Q2W_HAND_RIGHT;
  input.animate_player = player->character == QA_GAME_FAMILY_Q2;
  input.quad_until = facts.quad_until;
  input.double_until = 0; input.quad_fire_until = 0;
  input.haste = qa_app_shared_sim_lmctf_haste(sim, actor);
  bool rerelease = sim->q2_server_registry != NULL && qa_cvar_registry_dialect(sim->q2_server_registry) == QA_COMMAND_DIALECT_Q2_RERELEASE;
  input.no_stack_double = rerelease && qa_cvar_registry_variable_value(sim->q2_server_registry, "g_dm_no_stack_double") != 0;
  input.instant_switch = rerelease && qa_cvar_registry_variable_value(sim->q2_server_registry, "g_instant_weapon_switch") != 0;
  input.quick_switch = false;
  input.infinite_ammo = facts.infinite_ammo;
  input.players_collide = true; input.gravity = qa_app_sim_shared_physics_gravity(sim->physics); input.weapon_thunk = false;
  return input;
}

/* ---- equipmentWeaponInput (3022-3036) ---- */
qa_q2w_input qa_app_shared_sim_equipment_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor) {
  double now_milliseconds = qa_app_shared_sim_time_seconds(sim) * 1000;
  qa_app_source_equipment_powerups powers = sim->strategy->equipment_powerups(sim, actor);
  if (sim->strategy->has_guest_ui(sim)) {
    qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
    qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor, &body);
    qa_q2w_input input; memset(&input, 0, sizeof input);
    input.attack = sim->grapple ? qa_app_sim_grapple_runtime_held(sim->grapple, actor) : false;
    input.latched_attack = false; input.holster = false; input.angles = view.angles;
    input.ducked = (has_body ? body.bounds.max.z : 32) < 32;
    input.spectator = !qa_app_shared_sim_equipment_player_available(sim, actor);
    input.notarget = false; input.hand = QA_Q2W_HAND_RIGHT; input.animate_player = false;
    input.quad_until = powers.has_quad_until ? powers.quad_until : 0; input.double_until = 0; input.quad_fire_until = 0;
    input.haste = powers.haste_until > now_milliseconds || qa_app_shared_sim_lmctf_haste(sim, actor);
    input.no_stack_double = false; input.instant_switch = false; input.quick_switch = false; input.infinite_ammo = false;
    input.players_collide = true; input.gravity = qa_app_sim_shared_physics_gravity(sim->physics); input.weapon_thunk = false;
    return input;
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2w_input input = sim->strategy->refine_weapon_input(sim, actor, qa_app_shared_sim_q2_weapon_input(sim, player));
  if (powers.has_quad_until) input.quad_until = powers.quad_until;
  input.haste = input.haste || powers.haste_until > now_milliseconds;
  return input;
}
/* Q3Strategy.equipmentPowerups (records.byActor) / Q3QvmStrategy.equipmentPowerups */
qa_app_source_equipment_powerups qa_ss_q3_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_client *q3 = ss_q3_client(sim, actor);
  return (qa_app_source_equipment_powerups){true, (q3 ? ss_q3_powerup(q3, QA_Q3_PW_QUAD) : 0) / 1000, q3 ? ss_q3_powerup(q3, QA_Q3_PW_HASTE) : 0};
}
qa_app_source_equipment_powerups qa_ss_q3_qvm_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_qvm_state q3; ss_qvm_actor_state(sim, actor, &q3);
  return (qa_app_source_equipment_powerups){true, q3.powerups[QA_Q3_PW_QUAD] / 1000, q3.powerups[QA_Q3_PW_HASTE]};
}
/* Q3Strategy.weaponFacts (records.nativeByActor) */
qa_app_source_weapon_facts qa_ss_q3_weapon_facts(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_client *q3 = ss_q3_native_client(sim, actor);
  return (qa_app_source_weapon_facts){q3, false, "right", (q3 ? ss_q3_powerup(q3, QA_Q3_PW_QUAD) : 0) / 1000, false};
}
/* Q3Strategy.writeCharacterAnimation */
void qa_ss_q3_write_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_actor_animation_state *animation) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  if (entity != NULL) qa_app_write_q3_character_animation(entity, *animation);
}

/* ---- weaponCharacterAnimation (3043-3062) / grappleCharacterAnimation (3037-3041) ---- */
void qa_app_shared_sim_weapon_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee,
    qa_content_id content, bool reset_time) {
  bool attack = qa_ss_is(priority, "attack");
  if (sim->strategy->guest_weapon_animation(sim, actor, priority, melee)) return;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player == NULL) return;
  if (qa_ss_is(player->animation.state.kind, "q3")) {
    qa_q3_animation_context context; memset(&context, 0, sizeof context);
    context.animation = player->animation; context.dead = ss_health_or(sim, actor, 0) <= 0;
    context.elapsed_milliseconds = 0; context.buttons = player->buttons;
    context.product = strstr(sim->recipe.character.definition.content, "missionpack") != NULL ? "missionpack" : "baseq3";
    context.event_sequence = 0;
    qa_animation_step_result result = qa_q3_run_torso_operation(!attack ? QA_Q3_ANIM_TORSO_DROP : melee ? QA_Q3_ANIM_TORSO_ATTACK2 : QA_Q3_ANIM_TORSO_ATTACK,
        context, false);
    player->animation = result.animation;
    qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &player->actor);
    if (character) qa_q3_character_actor_commit_animation(*character, result.animation);
    sim->strategy->write_character_animation(sim, actor, &result.animation);
    free((void *)result.effects);
    return;
  }
  if (player->character != QA_GAME_FAMILY_Q2) return;
  bool ducked = player->bounds.max.z < player->standing_bounds.max.z;
  qa_q2w_frame_range frames = attack ? qa_q2w_attack_frames(ducked, 0) : qa_q2w_reverse_frames(ducked);
  qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &player->actor);
  if (character) qa_q2_character_actor_set_animation(*character, priority, frames.first, frames.last);
  qa_q2w_event event; memset(&event, 0, sizeof event);
  event.kind = QA_Q2W_EVENT_PLAYER_ANIMATION; event.actor = &actor; event.priority = priority;
  event.first = frames.first; event.last = frames.last; event.reset_time = reset_time;
  qa_app_shared_sim_weapon_event(sim, content, &event);
}
void qa_app_shared_sim_grapple_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee) {
  const qa_grapple_selection *equipment = &sim->recipe.equipment.grapple;
  if (!equipment->is_enabled) ss_fail("Grapple animation has no selected source");
  qa_app_shared_sim_weapon_character_animation(sim, actor, priority, melee, equipment->source.content, qa_ss_is(equipment->edition, "rerelease"));
}

/* ---- grappleSlotHost (3064-3075) ---- */
typedef struct ss_actor_ctx { qa_app_shared_simulation *sim; qa_actor_id actor; } ss_actor_ctx;
static bool ss_slot_selected(void *ctx, qa_actor_id actor) {
  qa_app_sim_weapon_slot *slot = ss_slot(ctx, actor);
  return slot ? qa_app_sim_weapon_slot_equipment_selected(slot) : false;
}
static void ss_slot_frame(void *ctx, qa_actor_id actor, double frame) {
  if (frame == 2) qa_app_shared_sim_grapple_character_animation(ctx, actor, "attack", true);
}
static bool ss_slot_available(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_equipment_player_available(ctx, actor); }
static void ss_presentation_attack(void *ctx) {
  ss_actor_ctx *record = ctx; qa_app_shared_sim_grapple_character_animation(record->sim, record->actor, "attack", false);
}
static void ss_presentation_reverse(void *ctx) {
  ss_actor_ctx *record = ctx; qa_app_shared_sim_grapple_character_animation(record->sim, record->actor, "reverse", false);
}
static double ss_presentation_animation_time(void *ctx, qa_q2w_state *state) {
  ss_actor_ctx *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_q2w_input input = qa_app_shared_sim_equipment_weapon_input(sim, record->actor);
  qa_q2w_animation_rate_input rate; memset(&rate, 0, sizeof rate);
  rate.quick_switch = input.quick_switch; rate.frame_seconds = qa_ss_seconds(sim->grapple_frame.elapsed);
  rate.phase = state->phase; rate.frame = state->frame; rate.quad_fire_until = input.quad_fire_until; rate.haste = input.haste;
  rate.now = qa_ss_seconds(sim->grapple_frame.time);
  return trunc(1000 / qa_q2w_animation_rate(rate)) / 1000;
}
static void ss_presentation_powerup_sound(void *ctx) {
  ss_actor_ctx *record = ctx; qa_app_shared_simulation *sim = record->sim;
  const qa_app_sim_grapple_source *source = sim->grapple ? qa_app_sim_grapple_runtime_source(sim->grapple) : NULL;
  if (source == NULL || qa_ss_is(source->kind, "q1-threewave") || qa_ss_is(source->kind, "q3-qvm")) return;
  qa_q2w_input input = qa_app_shared_sim_equipment_weapon_input(sim, record->actor);
  qa_q2_game_services *game = qa_q2_entity_services_game(source->q2_game);
  qa_q2w_powerup_sound_input sound = {input.quad_until, input.double_until,
      qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->edition, "rerelease"), game->host->now(game->host->ctx)};
  const char *path = qa_q2w_powerup_sound(sound);
  qa_body_state body;
  if (path == NULL || !qa_app_shared_sim_body(sim, record->actor, &body)) return;
  qa_q2_presentation_event event; memset(&event, 0, sizeof event);
  event.kind = "sound"; event.actor = record->actor; event.has_actor = true; event.origin = body.origin;
  event.path = path; event.channel = 3; event.volume = 1; event.attenuation = 1; event.reliable = false; event.loop = "once";
  game->host->emit(game->host->ctx, event);
}
static qa_grapple_weapon_presentation ss_slot_presentation(void *ctx, qa_actor_id actor) {
  ss_actor_ctx *record = qa_app_shared_sim_keep(ctx, sizeof *record);
  record->sim = ctx; record->actor = actor;
  qa_grapple_weapon_presentation presentation; memset(&presentation, 0, sizeof presentation);
  presentation.ctx = record; presentation.attack_animation = ss_presentation_attack; presentation.reverse_animation = ss_presentation_reverse;
  presentation.animation_time = ss_presentation_animation_time; presentation.powerup_sound = ss_presentation_powerup_sound;
  return presentation;
}
qa_app_sim_grapple_slot_host qa_app_shared_sim_grapple_slot_host(qa_app_shared_simulation *sim) {
  qa_app_sim_grapple_slot_host host = {sim, ss_slot_selected, ss_slot_available, ss_slot_frame, ss_slot_presentation};
  return host;
}

/* ---- selectedWeaponDelay (2555-2568) ---- */
typedef struct ss_delay_call { qa_app_shared_simulation *sim; qa_actor_id actor; double milliseconds, result; } ss_delay_call;
static void ss_native_weapon_delay(void *ctx) {
  ss_delay_call *call = ctx;
  call->result = qa_compat_q2_native_primary_weapons_weapon_delay(qa_app_shared_sim_require_native_weapons(call->sim), call->actor, call->milliseconds) / 1000;
}
/* TS nativeWeaponDelay (host member). */
double qa_app_shared_sim_native_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds) {
  {
    qa_item_id selected = NULL;
    if (ss_sel_has(sim, actor)) { qa_arsenal_state state = ss_sel_read(sim, actor); selected = state.active_weapon; ss_arsenal_free(&state); }
    const qa_ss_optional_item *item = NULL; qa_ss_optional_item none = {false, NULL};
    if (selected == NULL) item = &none; /* TS null */
    else if (sim->has_selected_original_weapons) item = qa_ss_map_get(&sim->selected_original_weapons, &selected);
    if (item == NULL || sim->native_primary_commands == NULL) ss_fail("Selected firing delay has no original weapon policy");
    ss_delay_call call = {sim, actor, seconds * 1000, 0};
    qa_compat_q2_native_primary_commands_with_weapon(sim->native_primary_commands, actor, item->has ? item->item : NULL, &call, ss_native_weapon_delay);
    return call.result;
  }
}
/* TS selectedWeaponDelay: this.strategy.weaponDelay(actor, seconds). */
double qa_app_shared_sim_selected_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds) {
  return sim->strategy->weapon_delay(sim, actor, seconds);
}
/* SourceStrategy weaponDelay members. */
double qa_ss_loaded_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds) {
  (void)sim; (void)actor;
  return seconds;
}
double qa_ss_q2_native_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds) {
  return qa_app_shared_sim_native_weapon_delay(sim, actor, seconds);
}
double qa_ss_q3_qvm_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds) {
  return qa_qvm_primary_weapons_weapon_delay(qa_app_shared_sim_require_qvm_weapons(sim), actor, trunc(seconds * 1000)) / 1000;
}
double qa_ss_q3_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds) {
  qa_q3_game_client *client = ss_q3_native_client(sim, actor);
  if (client == NULL) return seconds;
  qa_q3_base_product product = qa_q3_base_player_state_record_product(&client->ps);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(product);
  double persistent = schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      ? qa_q3_base_item_at(product, qa_q3_base_player_state_slots_get(&client->ps.stats, schema->persistent_powerup))->tag : 0;
  double milliseconds = seconds * 1000, delay = qa_q3_weapon_delay(milliseconds, persistent, ss_q3_powerup(client, QA_Q3_PW_HASTE) != 0);
  return delay == milliseconds ? seconds : delay / 1000;
}

/* ---- q2SourceDamageHooks (2570-2581): quadMultiplier and sourceDamageMultiplier. ---- */
double qa_app_shared_sim_q2_quad_multiplier(qa_app_shared_simulation *sim) {
  return sim->strategy->quad_factor(sim);
}
double qa_app_shared_sim_q2_source_damage_multiplier(qa_app_shared_simulation *sim, qa_actor_id actor) {
  /* this.strategy.weaponDamageFactor(actor, false) ?? 1 */
  double factor;
  return sim->strategy->weapon_damage_factor(sim, actor, false, &factor) ? factor : 1;
}
/* SourceStrategy weaponDamageFactor members: false = TS null. */
bool qa_ss_loaded_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out) {
  (void)sim; (void)actor; (void)quad; (void)out;
  return false;
}
bool qa_ss_quakec_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out) {
  (void)sim; (void)actor; (void)quad;
  *out = 1;
  return true;
}
bool qa_ss_q2_native_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out) {
  (void)quad;
  *out = qa_compat_q2_native_primary_weapons_damage_factor(qa_app_shared_sim_require_native_weapons(sim), actor);
  return true;
}
bool qa_ss_q3_qvm_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out) {
  (void)quad;
  *out = qa_qvm_primary_weapons_damage_factor(qa_app_shared_sim_require_qvm_weapons(sim), actor);
  return true;
}
bool qa_ss_q3_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out) {
  qa_q3_game_client *client = ss_q3_native_client(sim, actor);
  *out = client == NULL ? 1
      : qa_q3_weapon_damage_factor(client, quad ? qa_app_q3_source_runtime_quad_damage_factor(sim->source.q3) : 1,
                                   qa_q3_base_player_state_record_product(&client->ps));
  return true;
}

/* ---- handGrenadeInterval (3186-3194) ---- */
double qa_app_shared_sim_hand_grenade_interval(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds) {
  return sim->strategy->equipment_delay(sim, actor, provider, seconds);
}
/* SourceStrategy equipmentDelay members. The base answers with the strategy's own weaponDelay. */
double qa_ss_loaded_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds) {
  (void)provider;
  return sim->strategy->weapon_delay(sim, actor, seconds);
}
double qa_ss_q3_qvm_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds) {
  return qa_qvm_primary_weapons_equipment_delay(qa_app_shared_sim_require_qvm_weapons(sim), actor, provider, qa_js_round(seconds * 1000)) / 1000;
}
double qa_ss_q2_native_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds) {
  return qa_app_shared_sim_native_equipment_delay(sim, actor, provider, seconds);
}
/* TS nativeEquipmentDelay (host member). */
double qa_app_shared_sim_native_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds) {
  {
    const qa_compat_q2_native_primary_profile *profile = qa_app_shared_sim_native_primary(sim);
    if (profile == NULL || sim->native_primary_commands == NULL) ss_fail("Offhand equipment lost its original cadence owner");
    qa_item_id item = qa_source_equipment_item(profile->weapons->equipment_contexts, profile->weapons->equipment_context_count, provider);
    ss_delay_call call = {sim, actor, seconds * 1000, 0};
    qa_compat_q2_native_primary_commands_with_weapon(sim->native_primary_commands, actor, item, &call, ss_native_weapon_delay);
    return call.result;
  }
}

/* ---- stepHandGrenade (3221-3232) ---- */
typedef struct ss_grenade_project { qa_q2_game_services *game; qa_actor_id actor; qa_q2w_actor_view view; } ss_grenade_project;
static qa_q2w_projection ss_grenade_project_fn(void *ctx, qa_vec3 angles, qa_vec3 offset) {
  ss_grenade_project *project = ctx;
  return qa_q2w_project_actor(project->actor, project->game, project->view, angles, offset);
}
void qa_app_shared_sim_step_hand_grenade(qa_app_shared_simulation *sim, qa_actor_id actor, const char *kind) {
  qa_app_sim_hand_grenade_runtime *equipment = sim->hand_grenades;
  if (equipment == NULL) return;
  const char *lifecycle = kind != NULL ? kind : "alive";
  qa_q2w_input input = qa_app_shared_sim_equipment_weapon_input(sim, actor);
  qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  qa_app_source_equipment_suppression suppressed = sim->strategy->equipment_suppression(sim);
  ss_grenade_project project = {qa_q2_hand_grenade_equipment_game(qa_app_sim_hand_grenade_runtime_controller(equipment)), actor,
      {input.hand, view.view_height, input.players_collide}};
  qa_hand_grenade_equipment_input step; memset(&step, 0, sizeof step);
  step.angles = view.angles;
  step.gravity = qa_app_sim_shared_physics_gravity(sim->physics) * (player ? player->gravity_multiplier : 1);
  step.quad_until = suppressed.quad ? 0 : input.quad_until;
  step.double_until = input.double_until;
  step.quad_fire_until = suppressed.quad_fire ? 0 : input.quad_fire_until;
  step.haste = suppressed.haste ? false : input.haste;
  step.no_stack_double = input.no_stack_double; step.players_collide = input.players_collide;
  step.lifecycle = qa_ss_is(lifecycle, "dead") ? QA_Q2W_DEAD : qa_ss_is(lifecycle, "removing") ? QA_Q2W_REMOVING : QA_Q2W_ALIVE;
  step.project.ctx = &project; step.project.fn = ss_grenade_project_fn;
  bool enabled = qa_app_shared_sim_equipment_player_available(sim, actor) && !input.spectator;
  qa_app_sim_hand_grenade_runtime_step(equipment, actor, step, enabled);
}

/* ---- UserCommand member access (the TS union reads) ---- */
static double ss_buttons(const qa_user_command *command) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: return command->as.q1.buttons;
  case QA_USER_COMMAND_QW: return command->as.qw.buttons;
  case QA_USER_COMMAND_Q2: return command->as.q2.buttons;
  case QA_USER_COMMAND_Q2_RERELEASE: return command->as.q2_rerelease.buttons;
  case QA_USER_COMMAND_Q3: return command->as.q3.buttons;
  }
  return 0;
}
static void ss_set_buttons(qa_user_command *command, double buttons) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: command->as.q1.buttons = buttons; break;
  case QA_USER_COMMAND_QW: command->as.qw.buttons = buttons; break;
  case QA_USER_COMMAND_Q2: command->as.q2.buttons = buttons; break;
  case QA_USER_COMMAND_Q2_RERELEASE: command->as.q2_rerelease.buttons = buttons; break;
  case QA_USER_COMMAND_Q3: command->as.q3.buttons = buttons; break;
  }
}
/* "impulse" in command ? command.impulse : undefined. */
static bool ss_impulse(const qa_user_command *command, double *out) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: *out = command->as.q1.impulse; return true;
  case QA_USER_COMMAND_QW: *out = command->as.qw.impulse; return true;
  case QA_USER_COMMAND_Q2: *out = command->as.q2.impulse; return true;
  default: return false;
  }
}
static bool ss_attack_bit(double buttons) { return (qa_js_to_int32(buttons) & 1) != 0; }
static double ss_mask_attack(double buttons) { return (double)(qa_js_to_int32(buttons) & ~1); }
/* The weapon-step environment { health, flight: false, haste: false, invulnerable: false, gravityMultiplier: 1 }. */
static qa_movement_environment ss_weapon_environment(double health) {
  qa_movement_environment environment; memset(&environment, 0, sizeof environment);
  environment.health = health; environment.gravity_multiplier = 1;
  return environment;
}
/* A TS try { body } finally { restore } pair over qa_try. */
static void ss_finally(void (*body)(void *ctx), void *ctx, void (*restore)(void *ctx)) {
  qa_caught_error caught;
  bool ok = qa_try(body, ctx, &caught);
  restore(ctx);
  if (!ok) qa_rethrow(&caught);
}

/* ---- bindSelectedOriginalPickups (2428-2432) ---- */
typedef struct ss_pickup_binding { qa_app_shared_simulation *sim; qa_owned_actor actor; int64_t id; } ss_pickup_binding;
static void ss_unbind_pickup(void *ctx) {
  ss_pickup_binding *binding = ctx;
  qa_shared_inventory_table_unbind_pickup(binding->sim->inventory, binding->actor, binding->id);
}
void qa_app_shared_sim_bind_selected_original_pickups(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_ss_dispose *previous = qa_ss_map_get(&sim->selected_pickup_bindings, &actor.id);
  if (previous != NULL) previous->dispose(previous->ctx);
  qa_ss_map_delete(&sim->selected_pickup_bindings, &actor.id);
  if (sim->selected_original_pickup_count == 0) return;
  qa_inventory_pickup_binding requested = {sim->weapon_provider.provider, sim->selected_original_pickups, sim->selected_original_pickup_count};
  ss_pickup_binding *binding = qa_app_shared_sim_keep(sim, sizeof *binding);
  binding->sim = sim; binding->actor = actor;
  binding->id = qa_shared_inventory_table_bind_pickup(sim->inventory, actor, requested);
  qa_ss_dispose dispose = {binding, ss_unbind_pickup};
  qa_ss_map_set(&sim->selected_pickup_bindings, &actor.id, &dispose);
}

/* ---- bindSelectedAmmo (2537-2542) / selectedAmmo (2544-2549) / selectedAmmoActors (2551-2553) ---- */
static bool ss_ammo_is_live(void *ctx, qa_actor_id actor) { return qa_session_actor_registry_is_live(((qa_app_shared_simulation *)ctx)->actors, actor); }
static bool ss_ammo_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) { return ss_owned(ctx, actor, out); }
static void ss_ammo_assert_owned(void *ctx, qa_owned_actor actor) { qa_session_actor_registry_assert_owned(((qa_app_shared_simulation *)ctx)->actors, actor); }
static bool ss_ammo_resolve_saved(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_saved(((qa_app_shared_simulation *)ctx)->actors, saved, out);
}
void qa_app_shared_sim_bind_selected_ammo(qa_app_shared_simulation *sim, const qa_pickup_supply_profile *profile) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3) || qa_app_sim_provider_family(sim->weapon_provider.provider) == QA_GAME_FAMILY_Q3) return;
  if (sim->selected_ammo_timers != NULL) ss_fail("Selected ammo timers already have a source owner");
  const qa_pickup_supply_profile *legacy = qa_app_sim_provider_family(sim->weapon_provider.provider) == QA_GAME_FAMILY_Q1
      ? &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE : &QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE;
  qa_app_ammo_actor_registry actors = {sim, ss_ammo_assert_owned, ss_ammo_is_live, ss_ammo_resolve_owned, ss_ammo_resolve_saved};
  sim->selected_ammo_timers = qa_app_q3_mapped_ammo_regeneration_create(*profile, actors, qa_app_shared_sim_inventory_access(sim),
      legacy->ammo_owners, legacy->ammo_owner_count);
  if (sim->selected_ammo_timers == NULL) qa_range_error("Out of memory");
}
/* false = TS null. */
bool qa_app_shared_sim_selected_ammo(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_q3_ta_mapped_ammo_timer **out,
    size_t *out_count) {
  if (!sim->has_selected_arsenal || ss_family_is(sim, "q3") || !ss_sel_has(sim, actor)) return false;
  qa_owned_actor owner;
  bool owned = ss_owned(sim, actor, &owner);
  if (!owned || sim->selected_ammo_timers == NULL) ss_fail("Selected ammunition has no live timer owner");
  *out = qa_app_q3_mapped_ammo_regeneration_timers(sim->selected_ammo_timers, owner, out_count);
  return true;
}
const qa_owned_actor *qa_app_shared_sim_selected_ammo_actors(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_owned_actor *storage; static __thread size_t capacity;
  size_t count = 0;
  storage = ss_grow(storage, &capacity, sim->player_states.count + 1, sizeof *storage);
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    if (ss_sel_has(sim, player->actor.id)) storage[count++] = player->actor;
  }
  *out_count = count;
  return storage;
}

/* ---- nativeQ2WeaponInput (2101-2106) ---- */
qa_q2w_input qa_app_shared_sim_native_q2_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2w_input input = qa_app_shared_sim_equipment_weapon_input(sim, actor);
  const qa_mod_client_command_receipt *receipt = qa_ss_map_get(&sim->mod_client_commands, &actor);
  double buttons = receipt ? ss_buttons(&receipt->input.command) : 0;
  bool pressed = sim->has_native_weapon_turn && qa_same_actor(sim->native_weapon_turn_actor, actor) ? sim->native_weapon_turn_attack
      : ss_attack_bit(buttons);
  input.attack = pressed && qa_compat_q2_native_primary_weapons_available(qa_app_shared_sim_require_native_weapons(sim), actor)
      && ss_primary_selected(sim, actor);
  input.quad_until = 0; input.haste = false;
  return input;
}
/* ---- nativeClientSpawned (2107-2125) ---- */
static __thread ss_ui_storage ss_spawn_ui;
void qa_app_shared_sim_native_client_spawned(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE) || !sim->has_selected_arsenal) return;
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) return;
  if (!qa_shared_inventory_table_has(sim->inventory, actor)) qa_app_shared_sim_bind_equipment_inventory(sim, owner, NULL, 0);
  if (ss_sel_has(sim, actor)) ss_sel_remove(sim, actor);
  qa_ss_map_delete(&sim->mod_client_commands, &actor);
  qa_client_id client = qa_app_shared_sim_native_q2_client(sim, actor);
  double maximum = qa_compat_q2_native_primary_weapons_input(qa_app_shared_sim_require_native_weapons(sim), actor).max_health;
  const qa_app_sim_native_q2_travel_client *retained = qa_ss_map_has(&sim->native_travel_consumed, &client) ? NULL : ss_native_travel_client(sim, client);
  const qa_ss_selected_arsenal_travel *carry = retained ? retained->selected_arsenal : NULL;
  qa_ss_map_set(&sim->native_travel_consumed, &client, NULL);
  if (sim->selected_q3_source) qa_app_q3_selected_source_respawn(sim->selected_q3_source, actor);
  if (sim->selected_q3_source) qa_app_q3_selected_source_admit(sim->selected_q3_source, owner);
  if (carry == NULL) { qa_arsenal_state admitted = ss_sel_admit(sim, owner, maximum, qa_app_shared_sim_team_game(sim)); ss_arsenal_free(&admitted); }
  else qa_app_shared_sim_restore_selected_arsenal_travel(sim, owner, maximum, carry);
  if (!qa_ss_map_has(&sim->restored_native_inventory_selections, &actor) && sim->native_primary_inventory != NULL) {
    qa_app_arsenal_ui ui = ss_sel_ui(sim, actor, sim->weapon_provider, &ss_spawn_ui);
    qa_compat_q2_native_primary_inventory_restore(sim->native_primary_inventory, actor, ui.active_weapon);
  }
  double now = qa_compat_q2_native_primary_weapons_time_milliseconds(qa_app_shared_sim_require_native_weapons(sim));
  qa_ss_map_set(&sim->native_weapon_times, &actor, &now);
  qa_app_shared_sim_bind_selected_original_pickups(sim, owner); qa_app_shared_sim_bind_weapon_slot(sim, actor);
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, actor);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
}
/* ---- nativeWeaponStep (2126-2150) ---- */
typedef struct ss_weapon_turn {
  qa_app_shared_simulation *sim; qa_owned_actor actor; qa_actor_id id;
  qa_weapon_step_input input; const qa_arsenal_intent *intent;
  bool active; double health;
  bool before_has; qa_actor_id before_actor; bool before_attack;
  qa_mod_client_application *previous_application;
} ss_weapon_turn;
static void ss_native_turn_body(void *ctx) {
  ss_weapon_turn *turn = ctx; qa_app_shared_simulation *sim = turn->sim;
  bool attack = ss_attack_bit(ss_buttons(&turn->input.command));
  if (sim->selected_q3_source) qa_app_q3_selected_source_command(sim->selected_q3_source, turn->actor, attack, turn->active, turn->health > 0);
  turn->input.gauntlet_hit = turn->active && qa_ss_is(turn->input.arsenal.state.kind, "q3") && turn->input.arsenal.state.source_weapon == 1
      && attack && turn->health > 0 && sim->selected_q3_source ? qa_app_q3_selected_source_gauntlet_hit(sim->selected_q3_source, turn->actor) : false;
  qa_weapon_step_result result = ss_sel_step(sim, turn->input, turn->intent);
  (void)result;
  ss_reconcile(sim, turn->id);
}
static void ss_native_turn_restore(void *ctx) {
  ss_weapon_turn *turn = ctx;
  turn->sim->has_native_weapon_turn = turn->before_has; turn->sim->native_weapon_turn_actor = turn->before_actor;
  turn->sim->native_weapon_turn_attack = turn->before_attack;
  ss_arsenal_free(&turn->input.arsenal);
}
void qa_app_shared_sim_native_weapon_step(qa_app_shared_simulation *sim, qa_actor_id id, bool reached) {
  qa_client_id client;
  if (!ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE) || !sim->has_selected_arsenal || !qa_app_shared_sim_player_client(sim, id, &client)) return;
  qa_owned_actor actor;
  if (!ss_owned(sim, id, &actor)) return;
  if (!ss_sel_has(sim, id)) qa_app_shared_sim_native_client_spawned(sim, id);
  qa_compat_q2_native_primary_weapons *original = qa_app_shared_sim_require_native_weapons(sim);
  qa_mod_client_application *application = sim->native_weapon_application_count
      ? sim->native_weapon_applications[sim->native_weapon_application_count - 1] : NULL;
  const qa_mod_client_command_receipt *accepted = qa_ss_map_get(&sim->mod_client_commands, &id);
  bool from_application = application != NULL && qa_same_actor(application->identity.actor, id);
  const qa_user_command *input = from_application ? &application->command : accepted ? &accepted->input.command : NULL;
  if (input == NULL) return;
  double now = qa_compat_q2_native_primary_weapons_time_milliseconds(original);
  const double *stored = qa_ss_map_get(&sim->native_weapon_times, &id);
  double previous = stored ? *stored : now;
  double elapsed = qa_js_max(0, now - previous);
  qa_ss_map_set(&sim->native_weapon_times, &id, &now);
  bool active = reached && qa_compat_q2_native_primary_weapons_available(original, id) && ss_primary_selected(sim, id);
  ss_weapon_turn turn; memset(&turn, 0, sizeof turn);
  turn.sim = sim; turn.actor = actor; turn.id = id; turn.active = active;
  turn.input.actor = actor; turn.input.command = *input;
  if (!active) ss_set_buttons(&turn.input.command, ss_mask_attack(ss_buttons(input)));
  turn.input.arsenal = ss_sel_read(sim, id);
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, id);
  turn.health = combat.has_value ? combat.value.health : qa_app_shared_sim_native_player_ui(sim, id).health;
  qa_frame_context frame = ss_family_is(sim, "q1") ? qa_app_shared_sim_selected_q1_frame(sim)
      : sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2") ? sim->selected_weapon_source.frame
      : (qa_frame_context){sim->source_frame.frame, ss_milliseconds(now), sim->source_frame.elapsed, sim->source_frame.phase};
  frame.elapsed = ss_milliseconds(elapsed);
  turn.input.frame = frame;
  turn.input.animation.provider = sim->recipe.character.definition.provider;
  turn.input.animation.state = qa_compat_q2_native_primary_weapons_animation(original, id);
  turn.input.environment = ss_weapon_environment(turn.health);
  turn.intent = from_application ? (application->has_arsenal ? &application->arsenal : NULL) : accepted ? accepted->input.arsenal : NULL;
  turn.before_has = sim->has_native_weapon_turn; turn.before_actor = sim->native_weapon_turn_actor; turn.before_attack = sim->native_weapon_turn_attack;
  sim->has_native_weapon_turn = true; sim->native_weapon_turn_actor = id;
  sim->native_weapon_turn_attack = active && ss_attack_bit(ss_buttons(&turn.input.command));
  ss_finally(ss_native_turn_body, &turn, ss_native_turn_restore);
}

/* ---- qvmQ2WeaponInput (2151-2157) ---- */
qa_q2w_input qa_app_shared_sim_qvm_q2_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2w_input input = qa_app_shared_sim_equipment_weapon_input(sim, actor);
  qa_mod_client_application *application = sim->current_qvm_arsenal_application;
  double buttons = application != NULL && qa_same_actor(application->identity.actor, actor) ? ss_buttons(&application->command)
      : ss_family_is(sim, "q2") && ss_sel_has(sim, actor) ? qa_app_q2_selected_arsenal_capture_turn(sim->selected_arsenal.q2, actor).turn.buttons : 0;
  bool reached = sim->qvm_weapon_decision_count != 0 && sim->qvm_weapon_decisions[sim->qvm_weapon_decision_count - 1].reached;
  input.attack = qa_qvm_primary_weapons_available(qa_app_shared_sim_require_qvm_weapons(sim), actor, true)
      && (application == NULL || reached) && ss_attack_bit(buttons) && ss_primary_selected(sim, actor);
  input.quad_until = 0; input.haste = false;
  return input;
}
/* ---- qvmClientSpawned (2158-2167) ---- */
void qa_app_shared_sim_qvm_client_spawned(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3_QVM) || !sim->has_selected_arsenal) return;
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) return;
  if (ss_sel_has(sim, actor)) ss_sel_remove(sim, actor);
  qa_arsenal_state admitted = ss_sel_admit(sim, owner, qa_qvm_primary_weapons_max_health(qa_app_shared_sim_require_qvm_weapons(sim), actor),
      qa_app_shared_sim_team_game(sim));
  ss_arsenal_free(&admitted);
  if (sim->selected_q3_source) qa_app_q3_selected_source_respawn(sim->selected_q3_source, actor);
  if (sim->selected_q3_source) qa_app_q3_selected_source_admit(sim->selected_q3_source, owner);
  qa_app_shared_sim_bind_selected_original_pickups(sim, owner); qa_app_shared_sim_bind_weapon_slot(sim, actor);
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, actor);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
}
/* ---- qvmWeaponStep (2168-2190) ---- */
static void ss_qvm_turn_body(void *ctx) {
  ss_weapon_turn *turn = ctx; qa_app_shared_simulation *sim = turn->sim;
  bool attack = ss_attack_bit(ss_buttons(&turn->input.command));
  if (sim->selected_q3_source) qa_app_q3_selected_source_command(sim->selected_q3_source, turn->actor, attack, turn->active, turn->health > 0);
  turn->input.gauntlet_hit = turn->active && qa_ss_is(turn->input.arsenal.state.kind, "q3") && turn->input.arsenal.state.source_weapon == 1
      && attack && turn->health > 0 && sim->selected_q3_source ? qa_app_q3_selected_source_gauntlet_hit(sim->selected_q3_source, turn->actor) : false;
  qa_weapon_step_result result = ss_sel_step(sim, turn->input, turn->intent);
  (void)result;
  ss_reconcile(sim, turn->id);
}
static void ss_qvm_turn_restore(void *ctx) {
  ss_weapon_turn *turn = ctx;
  turn->sim->current_qvm_arsenal_application = turn->previous_application;
  ss_arsenal_free(&turn->input.arsenal);
}
void qa_app_shared_sim_qvm_weapon_step(qa_app_shared_simulation *sim, qa_mod_client_application *application, bool reached) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q3_QVM) || !sim->has_selected_arsenal) return;
  qa_owned_actor actor;
  if (!ss_owned(sim, application->identity.actor, &actor)) return;
  if (!ss_sel_has(sim, actor.id)) qa_app_shared_sim_qvm_client_spawned(sim, actor.id);
  if (!qa_qvm_primary_weapons_available(qa_app_shared_sim_require_qvm_weapons(sim), actor.id, true)) return;
  ss_qvm_state state; ss_qvm_actor_state(sim, actor.id, &state);
  ss_weapon_turn turn; memset(&turn, 0, sizeof turn);
  turn.sim = sim; turn.actor = actor; turn.id = actor.id;
  turn.previous_application = sim->current_qvm_arsenal_application;
  sim->current_qvm_arsenal_application = application;
  turn.input.arsenal = ss_sel_read(sim, actor.id);
  turn.active = ss_primary_selected(sim, actor.id) && reached;
  turn.input.actor = actor; turn.input.command = application->command;
  if (!turn.active) ss_set_buttons(&turn.input.command, ss_mask_attack(ss_buttons(&application->command)));
  turn.health = state.stats[0];
  turn.input.animation.provider = sim->recipe.character.definition.provider;
  turn.input.animation.state.kind = "q3"; turn.input.animation.state.legs = state.state.legs_animation;
  turn.input.animation.state.torso = state.state.torso_animation;
  turn.input.animation.state.legs_timer_milliseconds = state.state.legs_timer_milliseconds;
  turn.input.animation.state.torso_timer_milliseconds = state.state.torso_timer_milliseconds;
  qa_frame_context frame;
  if (ss_family_is(sim, "q1")) { frame = qa_app_shared_sim_selected_q1_frame(sim); frame.elapsed = application->frame.elapsed; }
  else if (sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2")) {
    frame = sim->selected_weapon_source.frame; frame.elapsed = application->frame.elapsed;
  } else { frame = application->frame; frame.time = ss_milliseconds(sim->selected_milliseconds); }
  turn.input.frame = frame;
  turn.input.environment = ss_weapon_environment(state.stats[0]);
  turn.intent = application->has_arsenal ? &application->arsenal : NULL;
  ss_finally(ss_qvm_turn_body, &turn, ss_qvm_turn_restore);
}

/* this.arsenal(player) as a transient read: the selected read's ammo array is freed. */
static void ss_arsenal_release(qa_app_shared_simulation *sim, qa_arsenal_state *state) {
  if (sim->has_selected_arsenal) ss_arsenal_free(state);
}

/* ---- weaponStep (4743-4805) ---- */
static qa_weapon_step_result weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_weapon_step_input input);
/* The returned effects belong to the player until its next weapon step (TS garbage). */
qa_weapon_step_result qa_app_shared_sim_weapon_step(qa_app_shared_simulation *sim, qa_weapon_step_input input) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player_of(sim, input.actor);
  if (player == NULL) ss_fail("Weapon input has no admitted player");
  free((void *)player->owned_weapon_effects);
  player->owned_weapon_effects = NULL;
  qa_weapon_step_result result = weapon_step(sim, player, input);
  player->owned_weapon_effects = result.effects;
  return result;
}
static qa_weapon_step_result weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_weapon_step_input input) {
  qa_actor_id actor = player->actor.id;
  /* Foreign LMCTF haste marks the weapon-stage environment only; movement
   * keeps its own environment, so the rune never speeds locomotion. */
  if (!input.environment.haste && qa_app_shared_sim_lmctf_haste(sim, input.actor.id)) input.environment.haste = true;
  qa_weapon_step_result passive; memset(&passive, 0, sizeof passive);
  if (sim->has_selected_arsenal) {
    if (ss_family_is(sim, "q1")) {
      qa_comp_q1_source_client *client = sim->strategy->q1_client(sim, actor);
      const qa_arsenal_intent *intent = player->has_arsenal_intent ? &player->arsenal_intent : NULL;
      double command_impulse = 0;
      if (!ss_impulse(&input.command, &command_impulse)) command_impulse = 0;
      /* client?.impulse || intent?.impulse || command impulse: 0 and NaN are falsy. */
      double impulse = client != NULL && client->impulse != 0 && !isnan(client->impulse) ? client->impulse
          : intent != NULL && intent->has_impulse && intent->impulse != 0 && !isnan(intent->impulse) ? intent->impulse : command_impulse;
      bool handled = qa_app_q1_selected_arsenal_impulse(sim->selected_arsenal.q1, actor, impulse);
      qa_app_shared_sim_sync_selected_q1_health_limit(sim, actor);
      if (handled) { if (client != NULL) client->impulse = 0; }
      else sim->strategy->q1_impulse(sim, actor);
      qa_q1_services *game = qa_app_shared_sim_selected_q1_game(sim);
      qa_q1_player_state *weapon_player = qa_q1_player(game, actor);
      if (intent != NULL && intent->has_impulse && (handled || client != NULL || (weapon_player != NULL && game->time >= weapon_player->attack_finished)))
        player->arsenal_intent.impulse = 0;
    } else sim->strategy->q1_impulse(sim, actor);
    if (sim->strategy->weapons_paused(sim)) {
      /* The selected read's ammo array is malloc'd: every caller stores the result as player.arsenal, so it is
       * adopted as the player's arsenal here (C ownership; freed when the next one replaces it). */
      qa_app_shared_sim_store_player_arsenal(sim, player, ss_sel_read(sim, actor));
      passive.arsenal = player->arsenal; passive.animation = input.animation;
      return passive;
    }
    qa_arsenal_state arsenal = ss_sel_read(sim, actor);
    double buttons = ss_buttons(&input.command);
    bool gauntlet_hit = ss_primary_selected(sim, actor) && qa_ss_is(arsenal.state.kind, "q3") && arsenal.state.source_weapon == 1
        && arsenal.state.time_milliseconds <= 0 && ss_attack_bit(buttons)
        && (input.command.kind != QA_USER_COMMAND_Q3 || (qa_js_to_int32(buttons) & QA_Q3_BUTTON_TALK) == 0)
        && input.environment.health > 0 && sim->selected_q3_source != NULL
        ? qa_app_q3_selected_source_gauntlet_hit(sim->selected_q3_source, player->actor) : false;
    qa_cleanup release; qa_cleanup_push(&release, ss_arsenal_free_cleanup, &arsenal); /* freed if the source raises */
    bool held = sim->strategy->step_selected_holdable(sim, player, &input);
    qa_cleanup_pop(&release);
    if (held) {
      qa_ss_map_set(&sim->primary_command_blocks, &actor, NULL);
      qa_app_shared_sim_store_player_arsenal(sim, player, arsenal); /* adopted as above */
      passive.arsenal = player->arsenal; passive.animation = input.animation;
      return passive;
    }
    qa_ss_map_delete(&sim->primary_command_blocks, &actor);
    qa_frame_context frame;
    if (ss_family_is(sim, "q1")) frame = qa_app_shared_sim_selected_q1_frame(sim);
    else if (sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2")) frame = sim->selected_weapon_source.frame;
    else { frame = input.frame; frame.time = ss_milliseconds(sim->selected_milliseconds); }
    ss_arsenal_free(&arsenal);
    qa_weapon_step_input step = input; step.gauntlet_hit = gauntlet_hit; step.frame = frame;
    qa_weapon_step_result result = ss_sel_step(sim, step, player->has_arsenal_intent ? &player->arsenal_intent : NULL);
    qa_app_shared_sim_store_player_arsenal(sim, player, result.arsenal);
    qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &player->actor);
    if (character) qa_q3_character_actor_commit_animation(*character, result.animation);
    ss_reconcile(sim, actor);
    return result;
  }
  qa_weapon_step_result stepped;
  if (sim->strategy->primary_weapon_step(sim, player, &input, &stepped)) return stepped;
  ss_reconcile(sim, actor);
  passive.arsenal = qa_app_shared_sim_arsenal(sim, player);
  qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &input.actor);
  passive.animation = character ? qa_q3_character_actor_get_animation(*character) : input.animation;
  return passive;
}
/* Q3Strategy.stepSelectedHoldable */
bool qa_ss_q3_step_selected_holdable(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input) {
  qa_actor_id actor = player->actor.id;
  qa_q3_game_client *client = ss_q3_native_client(sim, actor);
  if (client == NULL) ss_fail("Selected primary on Q3 map has no admitted source client");
  double buttons = ss_buttons(&input->command);
  bool use_holdable = player->has_arsenal_intent ? player->arsenal_intent.use_holdable
      : input->command.kind == QA_USER_COMMAND_Q3 && (qa_js_to_int32(buttons) & QA_Q3_BUTTON_USE_HOLDABLE) != 0;
  if ((qa_js_to_int32(buttons) & QA_Q3_BUTTON_ATTACK) == 0 && !use_holdable && input->environment.health > 0)
    client->ps.pm_flags = (double)(qa_js_to_int32(client->ps.pm_flags) & ~QA_Q3_FLAG_RESPAWNED);
  return !qa_app_shared_sim_owns_q3_equipment(sim) && qa_app_q3_source_runtime_step_holdable(sim->source.q3, actor, use_holdable);
}
/* TS host.ownsQ3Equipment(): this.selectedQ3Source?.ownsEquipment ?? false */
bool qa_app_shared_sim_owns_q3_equipment(qa_app_shared_simulation *sim) {
  return sim->selected_q3_source != NULL && qa_app_q3_selected_source_owns_equipment(sim->selected_q3_source);
}
/* Q3Strategy.primaryWeaponStep */
bool qa_ss_q3_primary_weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *in,
    qa_weapon_step_result *out) {
  qa_actor_id actor = player->actor.id;
  qa_weapon_step_input input = *in;
  {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
    qa_q3_arsenal_runtime_state *runtime = qa_ss_map_get(&sim->q3_arsenals, &player->actor);
    if (entity == NULL || entity->client == NULL || runtime == NULL) ss_fail("Q3 arsenal has no source client state");
    qa_q3_base_player_state *ps = &entity->client->ps;
    qa_weapon_step_input step = input;
    step.arsenal = qa_app_shared_sim_arsenal(sim, player);
    step.gauntlet_hit = player->has_source_movement ? player->source_movement.gauntlet_hit : input.gauntlet_hit;
    qa_arsenal_state controls_arsenal = qa_app_shared_sim_arsenal(sim, player);
    qa_q3_arsenal_controls controls = qa_app_sim_resolve_q3_arsenal_controls(controls_arsenal,
        player->has_arsenal_intent ? &player->arsenal_intent : NULL, input.command, runtime->product);
    qa_q3_arsenal_step result = qa_q3_step_arsenal(step, qa_app_read_q3_arsenal_runtime(entity, *runtime), controls, NULL, NULL);
    free((void *)result.torso_animations); /* nothing reads stepQ3Arsenal's torsoAnimations here */
    qa_ss_map_set(&sim->q3_arsenals, &player->actor, &result.runtime);
    player->arsenal = result.base.arsenal;
    free((void *)player->owned_arsenal_ammo); /* the previous step's array, now replaced */
    player->owned_arsenal_ammo = result.base.arsenal.ammo;
    qa_app_write_q3_arsenal_runtime(entity, result.runtime);
    for (size_t i = 0; i < result.base.arsenal.ammo_count; i++) ss_configure(sim, player->actor, result.base.arsenal.ammo[i]);
    if (qa_ss_is(result.base.arsenal.state.kind, "q3")) {
      ps->weapon = result.base.arsenal.state.source_weapon; ps->weapon_state = result.base.arsenal.state.state;
      ps->weapon_time = result.base.arsenal.state.time_milliseconds;
    }
    qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &player->actor);
    if (character) qa_q3_character_actor_commit_animation(*character, result.base.animation);
    ss_reconcile(sim, actor);
    *out = result.base;
    return true;
  }
}

/* ---- prepareArsenalCommand (4979-5019) ---- */
qa_actor_command qa_app_shared_sim_prepare_arsenal_command(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player,
    qa_actor_command received, bool paused) {
  static __thread qa_arsenal_intent intents[16]; static __thread size_t next_intent;
  qa_actor_command command = received;
  qa_actor_id actor = player->actor.id;
  if (command.arsenal != NULL && command.arsenal->has_impulse) {
    double impulse = command.arsenal->impulse;
    if (!isfinite(impulse) || impulse != trunc(impulse) || impulse < 0 || impulse > 255) qa_range_error("Source impulse must fit one byte");
  }
  qa_arsenal_intent *storage = &intents[next_intent++ % 16];
  if (sim->strategy->drops_impulse(sim) && command.arsenal != NULL && command.arsenal->has_impulse) {
    *storage = *command.arsenal; storage->impulse = 0; command.arsenal = storage;
  }
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  /* Remote peers choose these strings. An intent for a provider this player cannot use carries no request. */
  if (command.source.kind == QA_COMMAND_SOURCE_REMOTE_CLIENT && slot == NULL && command.arsenal != NULL
      && !qa_ss_is(command.arsenal->provider, sim->weapon_provider.provider)) {
    qa_arsenal_state current = qa_app_shared_sim_arsenal(sim, player);
    bool foreign = !qa_ss_is(command.arsenal->provider, current.provider);
    ss_arsenal_release(sim, &current);
    if (foreign) command.arsenal = NULL;
  }
  if (!paused && slot != NULL && command.arsenal != NULL) {
    qa_arsenal_intent intent = *command.arsenal;
    /* A refused request is ordinary play: a lagging client can select a weapon just after losing it. */
    if (intent.weapon != NULL) qa_app_shared_sim_request_weapon(sim, actor, (qa_weapon_reference){intent.provider, intent.weapon});
    *storage = intent; storage->provider = sim->weapon_provider.provider; storage->weapon = NULL; command.arsenal = storage;
  } else if (!paused && slot == NULL && !sim->has_selected_arsenal && command.arsenal != NULL
             && sim->strategy->requests_weapon_directly(sim)) {
    qa_arsenal_intent intent = *command.arsenal;
    if (!qa_ss_is(intent.provider, sim->weapon_provider.provider)) ss_fail("Arsenal command belongs to a different provider");
    if (intent.weapon != NULL) {
      bool request = sim->strategy->always_requests_weapon(sim);
      if (!request) {
        qa_arsenal_state current = qa_app_shared_sim_arsenal(sim, player);
        request = current.active_weapon == NULL || !qa_ss_is(intent.weapon, current.active_weapon);
        ss_arsenal_release(sim, &current);
      }
      if (request) qa_app_shared_sim_request_weapon(sim, actor, (qa_weapon_reference){intent.provider, intent.weapon});
    }
    *storage = intent; storage->provider = sim->weapon_provider.provider; storage->weapon = NULL; command.arsenal = storage;
  }
  if (!paused && sim->grapple != NULL && qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->binding, "slot"))
    qa_app_sim_grapple_runtime_input(sim->grapple, actor, ss_attack_bit(ss_buttons(&command.command)));
  qa_app_q3_selected_source *selected_ballistics = sim->selected_q3_source;
  if (!paused && selected_ballistics != NULL) {
    qa_arsenal_state arsenal = qa_app_shared_sim_arsenal(sim, player);
    bool primary = slot != NULL ? qa_app_sim_weapon_slot_primary_selected(slot) : true;
    double buttons = ss_buttons(&command.command);
    bool hook = primary && qa_ss_is(arsenal.state.kind, "q3") && arsenal.state.source_weapon == 10;
    ss_arsenal_release(sim, &arsenal);
    qa_app_q3_selected_source_command(selected_ballistics, player->actor,
        ss_attack_bit(buttons) && (command.command.kind != QA_USER_COMMAND_Q3 || (qa_js_to_int32(buttons) & QA_Q3_BUTTON_TALK) == 0),
        hook, !player->intermission && !player->has_cutscene && ss_health_or(sim, actor, 0) > 0);
    qa_vec3 point;
    bool has_point = qa_app_q3_selected_source_grapple_point(selected_ballistics, actor, &point);
    if (player->state.kind == QA_MOVEMENT_STATE_Q3) {
      qa_q3_movement_state *state = &player->state.as.q3;
      state->movement_flags = has_point ? (double)(qa_js_to_int32(state->movement_flags) | QA_Q3_FLAG_GRAPPLE_PULL)
          : (double)(qa_js_to_int32(state->movement_flags) & ~QA_Q3_FLAG_GRAPPLE_PULL);
      state->grapple_point = has_point ? point : qa_ss_zero;
    } else {
      qa_vec3 velocity; qa_body_state body;
      bool pulled = qa_app_q3_selected_source_pull(selected_ballistics, player->actor, &velocity);
      if (pulled && qa_app_shared_sim_body(sim, actor, &body)) { body.velocity = velocity; body.ground = NULL; qa_shared_body_table_write(ss_bodies(sim), player->actor, body); }
    }
  }
  return command;
}

/* ---- beginNativeEquipmentFrame (2881-2913) ---- */
static void ss_equipment_turn_position(void *ctx, qa_owned_actor actor, double out[2]) { qa_app_shared_sim_source_position(ctx, actor.id, out); }
void qa_app_shared_sim_begin_native_equipment_frame(qa_app_shared_simulation *sim) {
  qa_app_sim_grapple_runtime *grapple = sim->grapple; qa_app_sim_hand_grenade_runtime *grenades = sim->hand_grenades;
  if (grapple == NULL && grenades == NULL) return;
  qa_clock_profile clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
  if (grenades != NULL)
    sim->equipment_frame = qa_app_sim_provider_frame(sim->source_frame, clock,
        qa_app_sim_provider_timing(sim->recipe, qa_app_sim_hand_grenade_runtime_selection(grenades)->source.provider).clock);
  const qa_app_sim_grapple_source *grapple_source = grapple ? qa_app_sim_grapple_runtime_source(grapple) : NULL;
  if (grapple != NULL) {
    sim->grapple_frame = qa_app_sim_provider_frame(sim->source_frame, clock,
        qa_app_sim_provider_timing(sim->recipe, qa_app_sim_grapple_runtime_selection(grapple)->source.provider).clock);
    if (qa_ss_is(grapple_source->kind, "q1-threewave"))
      qa_q1_begin_frame(grapple_source->q1_game, qa_ss_seconds(sim->grapple_frame.time), qa_ss_seconds(sim->grapple_frame.elapsed));
    else if (qa_ss_is(grapple_source->kind, "q3-qvm"))
      qa_app_sim_qvm_grapple_source_begin_frame(grapple_source->q3_game, qa_js_round(qa_ss_seconds(sim->grapple_frame.time) * 1000), sim->grapple_frame.frame);
  }
  size_t player_count = 0;
  const qa_actor_id *listed = qa_app_shared_sim_players(sim, &player_count);
  qa_actor_id *players = calloc(player_count ? player_count : 1, sizeof *players);
  if (!players) qa_range_error("Out of memory");
  memcpy(players, listed, player_count * sizeof *players);
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id actor = players[i];
    qa_app_shared_sim_require_equipment_player(sim, actor);
    double teleport_bits;
    if (sim->strategy->teleport_bits(sim, actor, &teleport_bits) && grapple) qa_app_sim_grapple_runtime_observe_teleport(grapple, actor, teleport_bits);
    if (grapple_source != NULL && qa_ss_is(grapple_source->kind, "q3-qvm")) qa_app_sim_qvm_grapple_source_pull(grapple_source->q3_game, actor);
    bool available = qa_app_shared_sim_equipment_player_available(sim, actor);
    const bool *alive = qa_ss_map_get(&sim->native_equipment_alive, &actor);
    if (available && alive != NULL && !*alive) {
      if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, actor);
      if (grapple) qa_app_sim_grapple_runtime_release(grapple, actor);
      qa_app_shared_sim_admit_grapple(sim, actor);
    }
    qa_ss_map_set(&sim->native_equipment_alive, &actor, &available);
    if (sim->strategy->steps_hand_grenades(sim)) qa_app_shared_sim_step_hand_grenade(sim, actor, available ? "alive" : "dead");
    if (grapple) qa_app_sim_grapple_runtime_step(grapple, actor, available);
  }
  free(players);
  qa_app_sim_actor_visited visited = {0};
  qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(sim->actors, ss_equipment_turn_position, sim);
  qa_owned_actor actor;
  qa_q2_entity_services *independent = grenades ? qa_app_sim_hand_grenade_runtime_independent_game(grenades) : NULL;
  while (qa_app_sim_ordered_actor_turns_next(turns, &visited, &actor)) {
    const qa_app_sim_actor_execution *execution = qa_ss_map_get(&sim->actor_executions, &actor.id);
    if (execution == NULL || (execution->kind != QA_APP_SIM_ACTOR_EXECUTION_Q1 && execution->kind != QA_APP_SIM_ACTOR_EXECUTION_Q2)) continue;
    const void *services = execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? (const void *)execution->q1_services : (const void *)execution->q2_services;
    const void *grapple_game = grapple_source == NULL ? NULL : qa_ss_is(grapple_source->kind, "q1-threewave") ? (const void *)grapple_source->q1_game
        : (const void *)grapple_source->q2_game;
    const qa_frame_context *frame = grapple_game != NULL && services == grapple_game ? &sim->grapple_frame
        : independent != NULL && services == (const void *)independent ? &sim->equipment_frame : NULL;
    if (frame == NULL) continue;
    qa_app_sim_actor_execution entry = *execution;
    qa_app_sim_actor_execution_frame context = {sim->actors, ss_bodies(sim), sim->physics, sim->scheduler, *frame,
        qa_ss_seconds(frame->time), qa_ss_seconds(frame->elapsed), &visited};
    qa_app_sim_execute_actor(&entry, &context);
    qa_app_sim_shared_physics_commit_attachments(sim->physics);
  }
  qa_app_sim_ordered_actor_turns_destroy(turns);
  free(visited.actors);
  for (size_t i = 0; i < sim->weapon_slots.count; i++) qa_app_sim_weapon_slot_reconcile(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i));
}

/* ---- giveQ2StartItems (4595-4605) ---- */
void qa_app_shared_sim_give_q2_start_items(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q2)) return;
  qa_q2_product_runtime *source = sim->source.product;
  qa_q2_game_services *game = source->services;
  qa_owned_actor owned; bool has_owned = ss_owned(sim, actor, &owned);
  qa_q2_entity *entity = game->entity(game->ctx, &actor);
  qa_q2_base_player_state *state = qa_q2_players_get_state(source->players, actor);
  if (!has_owned || entity == NULL || state == NULL) ss_fail("Q2 starting inventory requires an admitted player");
  qa_q2_spawn_fields *fields = NULL; size_t field_count = 0;
  if (!qa_q2_parse_entities(sim->world_entities, game->options.edition, &fields, &field_count)) qa_range_error("Out of memory");
  const char *worldspawn_items = NULL;
  for (size_t i = 0; i < field_count; i++) if (qa_ss_is(fields[i].classname, "worldspawn")) {
    for (size_t j = 0; j < fields[i].value_count; j++) if (qa_ss_is(fields[i].values[j].key, "start_items")) worldspawn_items = fields[i].values[j].value;
    break;
  }
  /* this.startItems || worldspawn?.values.get("start_items") || "" */
  const char *expression = sim->start_items != NULL && sim->start_items[0] != '\0' ? sim->start_items
      : worldspawn_items != NULL && worldspawn_items[0] != '\0' ? worldspawn_items : "";
  if (expression[0] != '\0') qa_q2_item_module_give_start_items(source->items, owned, game, expression);
  qa_q2_entities_free(fields, field_count);
  qa_ss_map_delete(&sim->pending_start_items, &actor);
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
  qa_inventory_entry *spawn = malloc((count ? count : 1) * sizeof *spawn);
  if (!spawn) qa_range_error("Out of memory");
  memcpy(spawn, entries, count * sizeof *spawn);
  free(state->spawn_inventory);
  state->spawn_inventory = spawn; state->spawn_inventory_count = count;
  if (game->options.mode == QA_Q2_MODE_COOP) {
    /* state.coopRespawn = saveCarry(...): the state owns the carry's inventory, so the attach-time carry is released. */
    qa_q2_base_player_carry_clear(&state->coop_respawn);
    state->coop_respawn = qa_q2_players_save_carry(source->players, entity, game);
    state->has_coop_respawn = true;
  }
}

/* ---- grantSelectedArsenal (7690-7698) ---- */
static __thread ss_ui_storage ss_grant_ui;
bool qa_app_shared_sim_grant_selected_arsenal(qa_app_shared_simulation *sim, qa_actor_id actor, const char *category) {
  if (!sim->has_selected_arsenal) return false;
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) ss_fail("Selected grant actor is retired");
  bool weapons_category = qa_ss_is(category, "weapons");
  size_t native_count = 0;
  const qa_item_id *native_items = sim->strategy->guest_grant_exclusions(sim, &native_count);
  qa_app_arsenal_ui ui = ss_sel_ui(sim, actor, sim->recipe.map.entities, &ss_grant_ui);
  qa_arsenal_state state = ss_sel_read(sim, actor);
  for (size_t i = 0; i < state.ammo_count; i++) {
    const qa_inventory_entry *entry = &state.ammo[i];
    bool native = false;
    for (size_t j = 0; j < native_count && !native; j++) native = qa_ss_is(native_items[j], entry->item);
    bool weapon = false;
    for (size_t j = 0; j < ui.item_count; j++) if (qa_ss_is(ui.items[j].kind, "weapon") && qa_ss_is(ui.items[j].id, entry->item)) { weapon = true; break; }
    if (!native && weapons_category == weapon) {
      qa_inventory_entry configured = *entry; configured.count = weapons_category ? 1 : entry->capacity;
      ss_configure(sim, owner, configured);
    }
  }
  ss_arsenal_free(&state);
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor);
  return true;
}
/* ---- giveNativeSelectedAmmo (7700-7712) ---- */
void qa_app_shared_sim_give_native_selected_ammo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id source, const char *kind,
    double amount) {
  if (!ss_sel_has(sim, actor)) return;
  qa_owned_actor owner; bool owned = ss_owned(sim, actor, &owner);
  const qa_pickup_supply_mapping *mapping = NULL;
  if (sim->selected_original_supply != NULL)
    for (size_t i = 0; i < sim->selected_original_supply->ammo_count; i++)
      if (qa_ss_is(sim->selected_original_supply->ammo[i].source, source)) { mapping = &sim->selected_original_supply->ammo[i]; break; }
  if (!owned || mapping == NULL) return;
  for (size_t i = 0; i < mapping->destination_count; i++) {
    qa_item_id item = mapping->destinations[i];
    if (qa_ss_is(item, source)) continue;
    const qa_inventory_entry *entry = ss_entry(sim, actor, item);
    if (entry == NULL) ss_fail("Original ammo grant destination is unavailable: %s", item);
    double count = qa_js_max(0, qa_ss_is(kind, "set") ? amount : (double)qa_js_to_int32(entry->count + amount));
    qa_inventory_entry configured = *entry; configured.count = count; configured.capacity = qa_js_max(entry->capacity, count);
    ss_configure(sim, owner, configured);
  }
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor);
}

/* ---- the SimulationWeaponBehaviors targets closure (constructor 808-811) ---- */
qa_app_sim_qc_weapon_target *qa_app_shared_sim_weapon_behavior_targets(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_app_sim_qc_weapon_target *storage; static __thread size_t capacity;
  size_t observation_count = 0, count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  storage = ss_grow(storage, &capacity, observation_count + 1, sizeof *storage);
  for (size_t i = 0; i < observation_count; i++) {
    qa_actor_id actor = observations[i].id;
    qa_body_state body;
    if (!qa_app_shared_sim_body(sim, actor, &body)) continue;
    qa_app_sim_qc_weapon_target *target = &storage[count++];
    target->actor = actor; target->body = body; target->health = ss_health_or(sim, actor, 0);
    target->classname = qa_app_shared_sim_classname(sim, actor);
    target->name = qa_app_shared_sim_player(sim, actor) == NULL ? "" : qa_app_shared_sim_source_player_name(sim, actor);
    target->solid = qa_shared_body_table_linked(ss_bodies(sim), actor).has_value;
  }
  *out_count = count;
  return storage;
}

/* ---- PlayerUi storage: a ring of thread-local records, so a UI read stays
 * valid while a nested read (handoffs read the UI again) runs. ---- */
#define SS_UI_RING 4
typedef struct ss_player_ui_storage {
  ss_ui_storage arsenal;
  qa_app_sim_player_ui_item items[SS_UI_ITEMS];
  char labels[SS_UI_ITEMS][64];
  qa_active_powerup_timer powerups[32];
  qa_weapon_hud_status weapon_status;
  char status_label[64];
  qa_inventory_entry inventory[128];
  qa_app_player_ui_item guest_items[SS_UI_ITEMS];
  qa_compat_q2_native_inventory_readout native_inventory;
} ss_player_ui_storage;
static __thread ss_player_ui_storage ss_ui_ring[SS_UI_RING];
static __thread size_t ss_ui_next;
static ss_player_ui_storage *ss_ui_slot(void) {
  ss_player_ui_storage *slot = &ss_ui_ring[ss_ui_next++ % SS_UI_RING];
  return slot;
}
static qa_app_sim_player_ui_item ss_ui_item(const qa_app_player_ui_item *item) {
  qa_app_sim_player_ui_item copy = {item->id, item->label, item->kind, item->source_ordinal, item->owned, item->has_ammo,
      item->has_count, item->count, item->warning_count};
  return copy;
}
/* { ...ui, ...this.selectedArsenal.ui(actor, this.weaponProvider) } over a base UI. */
static void ss_spread_arsenal_ui(qa_app_sim_player_ui *ui, const qa_app_arsenal_ui *arsenal, ss_player_ui_storage *storage) {
  if (arsenal->has_weapon_status) { storage->weapon_status = arsenal->weapon_status; ui->weapon_status = &storage->weapon_status; }
  else ui->weapon_status = NULL;
  ui->arsenal_warning = arsenal->arsenal_warning;
  ui->active_weapon = arsenal->active_weapon;
  ui->has_ammo = arsenal->has_ammo; ui->ammo_item = arsenal->ammo_item; ui->ammo_count = arsenal->ammo_count;
  for (size_t i = 0; i < arsenal->item_count; i++) storage->items[i] = ss_ui_item(&arsenal->items[i]);
  ui->items = storage->items; ui->item_count = arsenal->item_count;
}
static double ss_ui_count(void *ctx, qa_item_id item) {
  ss_actor_ctx *record = ctx; return ss_count(record->sim, record->actor, item);
}
/* q1WeaponStatus over the Q1 foundation. */
static qa_item_id ss_q1_status_item(void *ctx, qa_q1_weapon weapon) { return qa_q1_services_weapon_item(ctx, weapon); }
static qa_item_id ss_q1_status_ammo(void *ctx, qa_q1_weapon weapon) { return qa_q1_services_weapon_ammo(ctx, weapon); }
static double ss_q1_status_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(((qa_q1_services *)ctx)->host.inventory, actor, item);
}
static bool ss_q1_status_available(void *ctx, qa_q1_player_state *player, qa_q1_weapon weapon, const char *purpose) {
  return qa_q1_weapon_available(ctx, player, weapon, purpose);
}
/* q3PowerupTimers' GameClient view. */
static double ss_q3_timer_expires(void *ctx, double powerup) { return ss_q3_powerup(ctx, powerup); }
static qa_app_sim_q3_timer_client ss_q3_timer_client(qa_q3_game_client *client) {
  qa_app_sim_q3_timer_client view; memset(&view, 0, sizeof view);
  if (client == NULL) return view;
  view.pm_flags = client->ps.pm_flags; view.client_num = client->ps.client_num; view.invulnerability_time = client->invulnerability_time;
  view.powerup_expires = ss_q3_timer_expires; view.powerup_ctx = client;
  return view;
}
static qa_app_sim_q3_timer_client ss_q3_timer_client_at(void *ctx, double number) {
  qa_q3_entity_pool *pool = ctx;
  return ss_q3_timer_client(number >= 0 && number < QA_Q3_MAX_CLIENTS ? pool->clients[(size_t)number] : NULL);
}

/* ---- primaryUi (6424-6476) ---- */
qa_app_sim_player_ui qa_app_shared_sim_primary_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  actor = sim->strategy->ui_subject(sim, actor);
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  if (!combat.has_value) ss_fail("Player has no combat state");
  ss_player_ui_storage *storage = ss_ui_slot();
  qa_app_sim_player_ui ui; memset(&ui, 0, sizeof ui);
  ui.health = combat.value.health; ui.armor = combat.value.armor; ui.arsenal_warning = "none";
  qa_app_sim_quakec_client_ui quakec_ui; bool has_quakec_ui = sim->strategy->client_ui(sim, actor, &quakec_ui);
  ui.powerup_count = sim->strategy->ui_powerups(sim, actor, storage->powerups, 32);
  ui.powerups = storage->powerups;
  ui.inventory = qa_shared_inventory_table_entries(sim->inventory, actor, &ui.inventory_count);
  if (sim->has_selected_arsenal) {
    if (has_quakec_ui) free(quakec_ui.weapon.items);
    qa_app_arsenal_ui arsenal = ss_sel_ui(sim, actor, sim->weapon_provider, &storage->arsenal);
    ss_spread_arsenal_ui(&ui, &arsenal, storage);
    return ui;
  }
  if (has_quakec_ui) {
    /* { health, armor, inventory, ...quakeCUi }: weapon_status null, arsenal_warning "none". */
    for (size_t i = 0; i < quakec_ui.powerup_count && i < 32; i++) storage->powerups[i] = quakec_ui.powerups[i];
    ui.powerup_count = quakec_ui.powerup_count < 32 ? quakec_ui.powerup_count : 32;
    ui.active_weapon = quakec_ui.weapon.active_weapon;
    ui.has_ammo = quakec_ui.weapon.has_ammo; ui.ammo_item = quakec_ui.weapon.ammo_item; ui.ammo_count = quakec_ui.weapon.ammo_count;
    size_t count = quakec_ui.weapon.item_count < SS_UI_ITEMS ? quakec_ui.weapon.item_count : SS_UI_ITEMS;
    for (size_t i = 0; i < count; i++) storage->items[i] = quakec_ui.weapon.items[i];
    free(quakec_ui.weapon.items);
    ui.items = storage->items; ui.item_count = count;
    return ui;
  }
  qa_arsenal_state arsenal = qa_app_shared_sim_arsenal(sim, player);
  sim->strategy->arsenal_ui(sim, actor, &arsenal, &ui, storage);
  ui.active_weapon = arsenal.active_weapon;
  return ui;
}
/* SourceStrategy.uiSubject (Q2): a spectating observer follows its connected chase target. */
qa_actor_id qa_ss_q2_ui_subject(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *observer = qa_q2_players_get_state(sim->source.product->players, actor);
  if (observer != NULL && observer->spectator && observer->has_chase_target) {
    qa_q2_base_player_state *watched = qa_q2_players_get_state(sim->source.product->players, observer->chase_target);
    if (watched != NULL && watched->connected && !watched->spectator) return observer->chase_target;
  }
  return actor;
}
/* SourceStrategy.uiPowerups per class (written into out, at most capacity). */
size_t qa_ss_q2_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity) {
  qa_q2_product_runtime *product = sim->source.product;
  qa_q2mp_powerups expansion; bool has_expansion = product->armory != NULL;
  if (has_expansion) expansion = qa_q2mp_items_powerups(&product->armory->items, actor);
  return qa_app_sim_q2_powerup_timers(qa_q2_item_module_player_powerups(product->items, actor), has_expansion ? &expansion : NULL,
      product->services->host->now(product->services->host->ctx), out, capacity);
}
size_t qa_ss_q1_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity) {
  qa_q1_player_state *state = qa_q1_player(sim->source.q1_game, actor);
  qa_app_sim_q1_powerup_entry entries[16]; size_t count = 0;
  if (state != NULL) for (size_t i = 0; i < state->powerup_count && i < 16; i++) entries[count++] = (qa_app_sim_q1_powerup_entry){state->powerups[i].kind, state->powerups[i].expires};
  return qa_app_sim_q1_powerup_timers(entries, count, sim->source.q1_game->time, out, capacity);
}
size_t qa_ss_q3_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity) {
  qa_q3_game_client *source_client = ss_q3_native_client(sim, actor);
  if (source_client == NULL) return 0;
  return qa_app_sim_q3_powerup_timers(ss_q3_timer_client(source_client), qa_app_q3_source_runtime_level_time(sim->source.q3),
      ss_q3_timer_client_at, qa_app_q3_source_runtime_pool(sim->source.q3), out, capacity);
}
size_t qa_ss_quakec_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity) {
  qa_app_sim_quakec_client_ui quakec_ui = qa_app_sim_quakec_source_client_ui(sim->source.quakec, actor);
  size_t count = quakec_ui.powerup_count < capacity ? quakec_ui.powerup_count : capacity;
  for (size_t i = 0; i < count; i++) out[i] = quakec_ui.powerups[i];
  free(quakec_ui.weapon.items);
  return count;
}
/* SourceStrategy.arsenalUi per class: items, ammo, weapon status and arsenal warning of the source's own arsenal. */
void qa_ss_loading_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage) {
  (void)sim; (void)actor; (void)arsenal;
  ss_player_ui_storage *storage = ui_storage;
  ui->items = storage->items; ui->item_count = 0; ui->has_ammo = false; ui->weapon_status = NULL; ui->arsenal_warning = "none";
}
void qa_ss_q1_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage) {
  (void)arsenal; /* TS Q1Strategy.arsenalUi(actor, _arsenal) reads the Q1 player instead */
  ss_player_ui_storage *storage = ui_storage;
  size_t item_count = 0;
  qa_q1_services *game = sim->source.q1_game;
  for (size_t ordinal = 0; ordinal < 8 + game->registered_weapon_count && item_count < SS_UI_ITEMS; ordinal++) {
    qa_q1_weapon weapon = ordinal < 8 ? QA_Q1_WEAPONS[ordinal] : game->registered_weapons[ordinal - 8].def->id;
    qa_item_id item = qa_q1_services_weapon_ammo(game, weapon);
    double count = item == NULL ? 0 : ss_count(sim, actor, item);
    const qa_q1_weapon_definition *registered = NULL;
    for (size_t j = 0; j < game->registered_weapon_count; j++) if (qa_ss_is(game->registered_weapons[j].def->id, weapon)) registered = game->registered_weapons[j].def;
    double per_shot = registered != NULL && registered->has_ammo_per_shot ? registered->ammo_per_shot
        : qa_ss_is(weapon, "supernailgun") || qa_ss_is(weapon, "supershotgun") ? 2 : 1;
    qa_app_sim_player_ui_item *entry = &storage->items[item_count++];
    memset(entry, 0, sizeof *entry);
    entry->id = qa_q1_services_weapon_item(game, weapon); entry->label = weapon; entry->kind = "weapon"; entry->source_ordinal = (double)ordinal;
    entry->owned = ss_count(sim, actor, entry->id) > 0;
    entry->has_count = item != NULL; entry->count = count;
    entry->has_ammo = item == NULL || count >= per_shot; entry->warning_count = 0;
  }
  qa_q1_player_state *state = qa_q1_player(game, actor);
  if (state != NULL) {
    qa_app_q1_status_host host = {game, ss_q1_status_item, ss_q1_status_ammo, ss_q1_status_count, ss_q1_status_available};
    storage->weapon_status = qa_app_q1_weapon_status(host, state, sim->weapon_provider, storage->status_label, sizeof storage->status_label);
    ui->weapon_status = &storage->weapon_status;
    qa_item_id item = qa_q1_services_weapon_ammo(game, state->weapon);
    if (item != NULL) { ui->has_ammo = true; ui->ammo_item = item; ui->ammo_count = ss_count(sim, actor, item); }
  }
  ui->items = storage->items; ui->item_count = item_count;
}
void qa_ss_q2_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage) {
  ss_player_ui_storage *storage = ui_storage;
  size_t item_count = 0;
  ss_actor_ctx count_ctx = {sim, actor};
  size_t list_count = 0;
  const qa_q2_inventory_item *list = qa_q2_item_module_list(sim->source.product->items, &list_count);
  for (size_t ordinal = 0; ordinal < list_count && item_count < SS_UI_ITEMS; ordinal++) {
    const qa_q2_inventory_item *definition = &list[ordinal];
    if (!definition->usable) continue;
    const qa_q2w_definition *weapon = ss_q2_definition(sim->source.q2_weapons, definition->id);
    double quantity = ss_count(sim, actor, definition->id);
    bool has_count = weapon == NULL || weapon->ammo != NULL;
    double count = weapon == NULL ? quantity : weapon->ammo == NULL ? 0 : ss_count(sim, actor, weapon->ammo);
    qa_app_sim_player_ui_item *entry = &storage->items[item_count++];
    memset(entry, 0, sizeof *entry);
    entry->id = definition->id; entry->label = definition->name; entry->kind = weapon == NULL ? "powerup" : "weapon";
    entry->source_ordinal = (double)ordinal; entry->owned = quantity > 0;
    entry->has_count = has_count; entry->count = count;
    entry->has_ammo = !has_count || count >= (weapon ? weapon->quantity : 1);
    entry->warning_count = weapon ? weapon->warning : 0;
  }
  const qa_q2w_definition *weapon = arsenal->active_weapon ? ss_q2_definition(sim->source.q2_weapons, arsenal->active_weapon) : NULL;
  if (qa_app_q2_weapon_status(weapon, ss_ui_count, &count_ctx, sim->weapon_provider, storage->status_label, sizeof storage->status_label,
          &storage->weapon_status)) ui->weapon_status = &storage->weapon_status;
  if (weapon != NULL && weapon->ammo != NULL) { ui->has_ammo = true; ui->ammo_item = weapon->ammo; ui->ammo_count = ss_count(sim, actor, weapon->ammo); }
  ui->items = storage->items; ui->item_count = item_count;
}
void qa_ss_q3_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage) {
  ss_player_ui_storage *storage = ui_storage;
  size_t item_count = 0;
  ss_actor_ctx count_ctx = {sim, actor};
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && item_count < SS_UI_ITEMS; i++) {
    const qa_q3_weapon_item *definition = &qa_q3_weapon_items[i];
    qa_app_sim_player_ui_item *entry = &storage->items[item_count];
    memset(entry, 0, sizeof *entry);
    snprintf(storage->labels[item_count], 64, "%s", definition->item + strlen("q3:weapon/"));
    item_count++;
    entry->id = definition->item; entry->label = storage->labels[item_count - 1]; entry->kind = "weapon";
    entry->source_ordinal = definition->weapon; entry->owned = ss_count(sim, actor, definition->item) > 0;
    entry->has_count = definition->ammo != NULL; entry->count = definition->ammo ? ss_count(sim, actor, definition->ammo) : 0;
    entry->has_ammo = !entry->has_count || entry->count > 0; entry->warning_count = 0;
  }
  const qa_q3_weapon_item *definition = qa_ss_is(arsenal->state.kind, "q3") ? qa_q3_get_weapon_item(arsenal->state.source_weapon) : NULL;
  if (definition != NULL && definition->ammo != NULL) { ui->has_ammo = true; ui->ammo_item = definition->ammo; ui->ammo_count = ss_count(sim, actor, definition->ammo); }
  const char *product = qa_app_q3_source_runtime_options(sim->source.q3)->product;
  if (qa_app_q3_weapon_status(arsenal->active_weapon, product, ss_ui_count, &count_ctx, sim->weapon_provider, storage->status_label,
          sizeof storage->status_label, &storage->weapon_status)) ui->weapon_status = &storage->weapon_status;
  ui->arsenal_warning = qa_app_q3_arsenal_warning(product, ss_ui_count, &count_ctx);
  ui->items = storage->items; ui->item_count = item_count;
}

/* ---- nativePlayerUi (5844-5869) ---- */
/* q2WeaponStatus(weapon, () => ammo, source) */
static double ss_constant_count(void *ctx, qa_item_id item) { (void)item; return *(const double *)ctx; }
static const qa_weapon_hud_status *ss_rerelease_status(void *ctx, const qa_q2w_definition *definition, double ammo, qa_provider_reference source) {
  ss_player_ui_storage *storage = ctx;
  if (!qa_app_q2_weapon_status(definition, ss_constant_count, &ammo, source, storage->status_label, sizeof storage->status_label,
          &storage->weapon_status)) return NULL;
  return &storage->weapon_status;
}
/* TS guestUi(actor) of the Q3 QVM and native Q2 strategies (the old nativePlayerUi); installed by source_strategy.c. */
qa_app_sim_player_ui qa_ss_q3_qvm_guest_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_player_ui_storage *storage = ss_ui_slot();
  qa_app_q3_player player;
  if (!ss_qvm_player_of(sim, actor, &player)) ss_fail("Actor has no Q3 guest client UI");
  ss_qvm_state state; ss_qvm_state_read(sim, player.source_entity, &state);
  size_t catalog_count = 0;
  const qa_q3_guest_weapon *catalog = qa_q3_guest_catalog_weapons(sim->source.qvm_catalog, &catalog_count);
  const qa_app_sim_q3_guest_options *guest = sim->options.q3_guest;
  /* this.options.q3Guest?.prepared.artifact.known?.product (the prepared artifact is a ResolvedQvmArtifact). */
  const qa_resolved_qvm_artifact *artifact = guest != NULL ? &guest->prepared->artifact : NULL;
  const char *product = artifact != NULL && artifact->known != NULL ? artifact->known->product : NULL;
  size_t inventory_count = 0;
  const qa_inventory_entry *inventory = sim->source.qvm_inventory != NULL ? qa_shared_inventory_table_entries(sim->inventory, actor, &inventory_count) : NULL;
  qa_item_id active = sim->source.qvm_weapons != NULL ? qa_qvm_primary_weapons_active(sim->source.qvm_weapons, actor) : NULL;
  qa_app_guest_player_ui scratch; memset(&scratch, 0, sizeof scratch);
  scratch.powerups = storage->powerups; scratch.powerup_capacity = 32;
  scratch.inventory = storage->inventory; scratch.inventory_capacity = 128;
  scratch.items = storage->guest_items; scratch.item_capacity = SS_UI_ITEMS;
  qa_app_guest_player_ui original = qa_app_q3_guest_player_ui(state.state, sim->recipe.map.entities, qa_app_shared_sim_time_seconds(sim) * 1000,
      catalog, catalog_count, product, inventory, inventory_count, sim->source.qvm_inventory != NULL, active, sim->source.qvm_weapons != NULL, scratch);
  qa_app_sim_player_ui ui; memset(&ui, 0, sizeof ui);
  ui.powerups = original.powerups; ui.powerup_count = original.powerup_count;
  ui.health = original.health; ui.armor = original.armor; ui.active_weapon = original.active_weapon;
  ui.has_ammo = original.has_ammo; ui.ammo_item = original.ammo_item; ui.ammo_count = original.ammo_count;
  ui.inventory = original.inventory; ui.inventory_count = original.inventory_count;
  if (original.has_weapon_status) { storage->weapon_status = original.weapon_status; ui.weapon_status = &storage->weapon_status; }
  ui.arsenal_warning = original.arsenal_warning;
  for (size_t i = 0; i < original.item_count; i++) storage->items[i] = ss_ui_item(&original.items[i]);
  ui.items = storage->items; ui.item_count = original.item_count;
  if (ss_sel_has(sim, actor)) {
    ui.inventory = qa_shared_inventory_table_entries(sim->inventory, actor, &ui.inventory_count);
    qa_app_arsenal_ui arsenal = ss_sel_ui(sim, actor, sim->weapon_provider, &storage->arsenal);
    ss_spread_arsenal_ui(&ui, &arsenal, storage);
  }
  if (sim->source.qvm_combat == NULL) return ui;
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  if (!combat.has_value) ss_fail("Qualified Q3 guest client has no combat binding");
  ui.armor = combat.value.armor;
  return ui;

}
qa_app_sim_player_ui qa_ss_q2_native_guest_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_player_ui_storage *storage = ss_ui_slot();
  qa_provider_reference reference = sim->recipe.map.entities;
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  size_t configstring_count = 0, definition_count = 0;
  const qa_q2w_base_definition *definitions = qa_q2w_base_weapons; definition_count = qa_q2w_base_weapon_count;
  qa_app_sim_player_ui original;
  if (ss_native_classic(sim)) {
    const qa_app_sim_configstring *configstrings = qa_app_sim_classic_guest_world_configstrings(sim->source.classic_world, &configstring_count);
    original = qa_app_sim_classic_guest_player_ui(qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot), configstrings,
        configstring_count, reference, definitions, definition_count, &storage->weapon_status, storage->status_label, sizeof storage->status_label);
  } else {
    const qa_app_sim_configstring *configstrings = qa_app_sim_rerelease_guest_world_configstrings(sim->source.rerelease_world, &configstring_count);
    original = qa_app_sim_rerelease_guest_player_ui(qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot),
        (const qa_app_sim_rerelease_guest_configstring *)configstrings, configstring_count, reference, definitions, definition_count,
        ss_rerelease_status, storage);
  }
  bool has_native = sim->native_primary_inventory != NULL
      && qa_compat_q2_native_primary_inventory_read(sim->native_primary_inventory, actor, &storage->native_inventory);
  if (!ss_sel_has(sim, actor)) {
    if (!has_native) return original;
    original.native_inventory = &storage->native_inventory;
    original.inventory = qa_shared_inventory_table_entries(sim->inventory, actor, &original.inventory_count);
    return original;
  }
  original.selected_arsenal = true;
  if (has_native) original.native_inventory = &storage->native_inventory;
  original.inventory = qa_shared_inventory_table_entries(sim->inventory, actor, &original.inventory_count);
  qa_app_arsenal_ui arsenal = ss_sel_ui(sim, actor, sim->weapon_provider, &storage->arsenal);
  ss_spread_arsenal_ui(&original, &arsenal, storage);
  return original;

}
qa_app_sim_player_ui qa_app_shared_sim_native_player_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return sim->strategy->guest_ui(sim, actor);
}

/* ---- slotProjection (5709-5741) ---- */
#define SS_SLOT_SOURCES 8
typedef struct ss_slot_storage {
  qa_app_sim_weapon_slot_projection projection; /* ui.items malloc'd by the projection */
  qa_weapon_reference primary_active, primary_pending;
  qa_weapon_reference source_active[SS_SLOT_SOURCES], source_pending[SS_SLOT_SOURCES];
  qa_app_sim_source_weapon_projection sources[SS_SLOT_SOURCES];
  qa_app_simulation_presentation models[SS_SLOT_SOURCES + 1];
  qa_weapon_hud_status statuses[SS_SLOT_SOURCES];
  qa_app_sim_player_ui_item items[SS_SLOT_SOURCES][32];
  qa_app_sim_presentation_attachment attachments[8];
  qa_app_sim_equipment_weapon_projection equipment;
} ss_slot_storage;
static __thread ss_slot_storage ss_slot_ring[SS_UI_RING];
static __thread size_t ss_slot_next;
static qa_game_family ss_content_family(qa_content_id content) {
  return strncmp(content, "q1:", 3) == 0 ? QA_GAME_FAMILY_Q1 : strncmp(content, "q2:", 3) == 0 ? QA_GAME_FAMILY_Q2 : QA_GAME_FAMILY_Q3;
}
qa_app_sim_weapon_slot_projection qa_app_shared_sim_slot_projection(qa_app_shared_simulation *sim, qa_actor_id actor,
    const qa_app_simulation_presentation *model) {
  ss_slot_storage *storage = &ss_slot_ring[ss_slot_next++ % SS_UI_RING];
  free((void *)storage->projection.ui.items);
  memset(storage, 0, sizeof *storage);
  bool native = sim->strategy->has_guest_ui(sim);
  qa_app_sim_movement_player *player = native ? NULL : qa_app_shared_sim_require_player(sim, actor);
  qa_app_sim_player_ui ui = native ? sim->strategy->guest_ui(sim, actor) : qa_app_shared_sim_primary_ui(sim, actor);
  bool has_arsenal = player != NULL;
  qa_arsenal_state arsenal; memset(&arsenal, 0, sizeof arsenal);
  if (has_arsenal) arsenal = qa_app_shared_sim_arsenal(sim, player);
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  qa_app_sim_grapple_runtime *grapple = sim->grapple;
  bool has_q3_pending = false; double q3_pending = 0;
  if (player == NULL) {
    const double *requested = qa_ss_map_get(&sim->native_weapon_requests, &actor);
    if (requested) { has_q3_pending = true; q3_pending = *requested; }
  } else {
    const qa_q3_arsenal_runtime_state *runtime = qa_ss_map_get(&sim->q3_arsenals, &player->actor);
    if (runtime && runtime->has_requested_weapon) { has_q3_pending = true; q3_pending = runtime->requested_weapon; }
  }
  qa_item_id pending_item = NULL;
  if (ss_sel_has(sim, actor)) pending_item = ss_sel_pending(sim, actor);
  else if (has_arsenal && qa_ss_is(arsenal.state.kind, "q2")) pending_item = arsenal.state.pending_weapon;
  else if (has_q3_pending) { const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(q3_pending); pending_item = weapon ? weapon->item : NULL; }
  if (has_arsenal) ss_arsenal_release(sim, &arsenal);
  qa_app_sim_primary_weapon_projection primary; memset(&primary, 0, sizeof primary);
  if (ui.active_weapon != NULL) { storage->primary_active = (qa_weapon_reference){sim->weapon_provider.provider, ui.active_weapon}; primary.active = &storage->primary_active; }
  if (pending_item != NULL) { storage->primary_pending = (qa_weapon_reference){sim->weapon_provider.provider, pending_item}; primary.pending = &storage->primary_pending; }
  primary.ui = ui; primary.model = model;
  if (slot == NULL) { storage->projection = primary; storage->projection.ui.items = NULL; return primary; }
  qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
  qa_vec3 eye = qa_ss_add(view.origin, (qa_vec3){0, 0, view.view_height});
  bool visible_owner = ui.health > 0 && (player == NULL || (!player->intermission && !player->has_cutscene));
  size_t presentation_count = 0;
  const qa_source_weapon_presentation *presentations = qa_app_sim_weapon_slot_presentations(slot, &presentation_count);
  if (presentation_count > SS_SLOT_SOURCES) ss_fail("Weapon slot has more sources than the projection holds");
  for (size_t i = 0; i < presentation_count; i++) {
    const qa_source_weapon_presentation *source = &presentations[i];
    qa_app_sim_source_weapon_projection *projected = &storage->sources[i];
    const qa_source_item_definition *active = NULL;
    size_t definition_count = 0;
    for (size_t j = 0; j < source->item_count; j++) if (source->items[j].is_weapon) {
      const qa_source_item_definition *definition = &source->items[j];
      if (active == NULL && source->active != NULL && qa_ss_is(definition->item, source->active)) active = definition;
      if (definition_count == 32) ss_fail("Weapon slot source has more weapons than the projection holds");
      qa_app_sim_player_ui_item *item = &storage->items[i][definition_count];
      item->id = definition->item; item->label = definition->label; item->kind = "weapon";
      item->source_ordinal = (double)(ui.item_count + definition_count);
      item->owned = ss_count(sim, actor, definition->item) > 0;
      item->has_count = definition->weapon.ammo != NULL;
      item->count = definition->weapon.ammo ? ss_count(sim, actor, definition->weapon.ammo) : 0;
      item->has_ammo = definition->weapon.ammo == NULL || item->count > 0; item->warning_count = 0;
      definition_count++;
    }
    qa_item_id ammo_item = active ? active->weapon.ammo : NULL;
    projected->provider = source->source.provider;
    if (source->active != NULL) { storage->source_active[i] = (qa_weapon_reference){source->source.provider, source->active}; projected->active = &storage->source_active[i]; }
    if (source->pending != NULL) { storage->source_pending[i] = (qa_weapon_reference){source->source.provider, source->pending}; projected->pending = &storage->source_pending[i]; }
    if (ammo_item != NULL) { projected->has_ammo = true; projected->ammo_item = ammo_item; projected->ammo_count = ss_count(sim, actor, ammo_item); }
    if (active != NULL) {
      qa_weapon_hud_status *status = &storage->statuses[i];
      status->source = source->source; status->item = active->item; status->label = active->label;
      status->ammo_is_finite = ammo_item != NULL;
      if (ammo_item != NULL) { status->ammo_item = ammo_item; status->ammo_count = projected->ammo_count; status->has_ammo_to_start = projected->ammo_count > 0; status->low = false; }
      projected->weapon_status = status;
    }
    const qa_source_weapon_presentation_model *asset = source->model;
    const qa_held_weapon_declaration *held = active ? active->weapon.held : NULL;
    if (asset != NULL || held != NULL) {
      qa_app_simulation_presentation *presentation = &storage->models[i];
      presentation->actor = actor; presentation->content = source->source.content; presentation->held_weapon = held;
      presentation->family = ss_content_family(source->source.content);
      presentation->path = asset == NULL ? "" : asset->is_resolved ? asset->resource.requested_path : asset->path;
      presentation->frame = asset ? asset->frame : 0; presentation->old_frame = presentation->frame;
      presentation->origin = eye; presentation->has_view_origin = true; presentation->view_origin = eye;
      presentation->has_view_angles = true; presentation->view_angles = view.angles; presentation->angles = view.angles;
      presentation->scale = 1; presentation->visible = asset != NULL && visible_owner; presentation->view_weapon = true;
      if (asset != NULL && asset->is_resolved)
        qa_app_sim_simulation_events_register_resource(sim->events, source->source.content, asset->resource.requested_path, asset->resource);
      projected->model = presentation;
    }
    projected->items = storage->items[i]; projected->item_count = definition_count;
  }
  if (grapple == NULL || !qa_ss_is(qa_app_sim_grapple_runtime_selection(grapple)->binding, "slot")) {
    storage->projection = qa_app_sim_project_weapon_slot(qa_app_sim_weapon_slot_snapshot(slot), primary, NULL, storage->sources, presentation_count);
    return storage->projection;
  }
  const qa_grapple_selection *selection = qa_app_sim_grapple_runtime_selection(grapple);
  const qa_app_sim_grapple_source *grapple_source = qa_app_sim_grapple_runtime_source(grapple);
  qa_app_sim_grapple_weapon_view gear;
  bool has_gear = qa_app_sim_grapple_runtime_weapon_view(grapple, actor, &gear);
  qa_weapon_reference weapon = qa_app_sim_grapple_runtime_weapon(grapple);
  qa_app_simulation_presentation *gear_model = NULL;
  if (has_gear) {
    gear_model = &storage->models[SS_SLOT_SOURCES];
    gear_model->actor = actor; gear_model->content = selection->source.content;
    gear_model->family = qa_ss_is(grapple_source->kind, "q1-threewave") ? QA_GAME_FAMILY_Q1 : qa_ss_is(grapple_source->kind, "q3-qvm") ? QA_GAME_FAMILY_Q3 : QA_GAME_FAMILY_Q2;
    gear_model->path = gear.path; gear_model->frame = gear.frame; gear_model->old_frame = gear.frame;
    if (gear.model_attachments != NULL) {
      size_t count = gear.model_attachment_count < 8 ? gear.model_attachment_count : 8;
      for (size_t i = 0; i < count; i++) storage->attachments[i] = (qa_app_sim_presentation_attachment){gear.model_attachments[i].path, gear.model_attachments[i].tag};
      gear_model->model_attachments = storage->attachments; gear_model->model_attachment_count = count;
    }
    if (gear.has_q3) {
      gear_model->model_anchor = gear.model_anchor;
      gear_model->has_q3_weapon = true; gear_model->q3_weapon_time_ms = gear.q3_time_milliseconds;
      gear_model->q3_weapon_torso_animation = gear.q3_torso_animation; gear_model->q3_weapon_firing = gear.q3_firing;
      gear_model->q3_weapon_horizontal_speed = gear.q3_horizontal_speed; gear_model->q3_weapon_bob_cycle = gear.q3_bob_cycle;
      gear_model->q3_weapon_weapon = gear.q3_weapon;
    }
    gear_model->origin = qa_ss_add(eye, gear.kick_origin);
    gear_model->has_view_origin = true; gear_model->view_origin = eye;
    gear_model->has_view_angles = true; gear_model->view_angles = view.angles;
    gear_model->angles = qa_ss_add(view.angles, (qa_vec3){gear.kick_pitch, 0, 0});
    gear_model->scale = 1; gear_model->visible = visible_owner; gear_model->view_weapon = true;
  }
  qa_app_sim_equipment_weapon_projection *equipment = &storage->equipment;
  equipment->source = selection->source; equipment->weapon = weapon;
  equipment->item.id = weapon.item; equipment->item.label = qa_ss_is(selection->mechanic, "q2-lmctf") ? "Hook" : "Grapple";
  equipment->item.kind = "weapon"; equipment->item.source_ordinal = (double)ui.item_count;
  equipment->item.owned = ss_count(sim, actor, weapon.item) > 0; equipment->item.has_ammo = true; equipment->item.has_count = false;
  equipment->item.warning_count = 0;
  equipment->model = gear_model;
  storage->projection = qa_app_sim_project_weapon_slot(qa_app_sim_weapon_slot_snapshot(slot), primary, equipment, storage->sources, presentation_count);
  return storage->projection;
}
/* ---- playerUi (5870) ---- */
qa_app_sim_player_ui qa_app_shared_sim_player_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_app_shared_sim_slot_projection(sim, actor, NULL).ui;
}
/* ---- weaponSlot (6420-6422) ---- */
qa_app_sim_weapon_slot_projection qa_app_shared_sim_weapon_slot(qa_app_shared_simulation *sim, qa_actor_id actor) {
  size_t count = 0;
  const qa_app_simulation_presentation *models = qa_app_shared_sim_primary_presentations(sim, &count);
  const qa_app_simulation_presentation *model = NULL;
  for (size_t i = 0; i < count; i++) if (models[i].view_weapon && qa_same_actor(models[i].actor, actor)) { model = &models[i]; break; }
  return qa_app_shared_sim_slot_projection(sim, actor, model);
}

/* this.events.message({ kind: "print", level: 2, text }, actor) */
static void ss_print_to(qa_app_shared_simulation *sim, qa_actor_id actor, const char *text) { qa_app_shared_sim_print(sim, text, &actor); }
static const qa_source_item_definition *ss_item_definition(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  size_t count = 0;
  const qa_source_item_definition *definitions = qa_shared_inventory_table_item_definitions(sim->inventory, actor, &count);
  for (size_t i = 0; i < count; i++) if (qa_ss_is(definitions[i].item, item)) return &definitions[i];
  return NULL;
}
static bool ss_sel_catalog_has(qa_app_shared_simulation *sim, qa_item_id item) {
  size_t count = 0;
  const qa_selected_pickup_weapon *catalog = ss_sel_catalog(sim, &count);
  for (size_t i = 0; i < count; i++) if (qa_ss_is(catalog[i].item, item)) return true;
  return false;
}

/* ---- useSelectedInventoryItem (5834-5843) ---- */
void qa_app_shared_sim_use_selected_inventory_item(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  const qa_source_item_definition *definition = ss_item_definition(sim, actor, item);
  if ((definition != NULL && definition->is_weapon) || (sim->has_selected_arsenal && ss_sel_catalog_has(sim, item))) {
    qa_weapon_reference weapon = {definition != NULL ? definition->source.provider : sim->weapon_provider.provider, item};
    qa_app_shared_sim_request_weapon(sim, actor, weapon);
    return;
  }
  qa_source_item_action_invocation action = qa_shared_inventory_table_item_action(sim->inventory, actor, item, QA_SOURCE_ITEM_ACTION_USE);
  if (action.has_value) { action.call(action.ctx); return; }
  if (ss_family_is(sim, "q3") && qa_app_q3_selected_arsenal_use_item(sim->selected_arsenal.q3, actor, item)) return;
  ss_fail("Admitted selected inventory item has no use owner");
}

/* "Ambiguous <noun>; use a, b\n" */
static void ss_print_ambiguous(qa_app_shared_simulation *sim, qa_actor_id actor, const char *noun, const qa_source_item_named_entry *rows,
    const size_t *indices, size_t count) {
  char text[2048]; size_t length = (size_t)snprintf(text, sizeof text, "Ambiguous %s; use ", noun);
  for (size_t i = 0; i < count && length < sizeof text; i++)
    length += (size_t)snprintf(text + length, sizeof text - length, "%s%s", i ? ", " : "", rows[indices[i]].item);
  if (length < sizeof text) snprintf(text + length, sizeof text - length, "\n");
  ss_print_to(sim, actor, text);
}
/* args.join(" ") */
static void ss_join(const char *const *args, size_t count, char *out, size_t capacity) {
  size_t length = 0; out[0] = '\0';
  for (size_t i = 0; i < count && length < capacity; i++) length += (size_t)snprintf(out + length, capacity - length, "%s%s", i ? " " : "", args[i]);
}

/* ---- selectedWeaponCommand (7764-7776) ---- */
bool qa_app_shared_sim_selected_weapon_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name,
    const char *const *args, size_t arg_count) {
  if (!sim->has_selected_arsenal || !(qa_ss_is(name, "use") || qa_ss_is(name, "weapon") || qa_ss_is(name, "weapnext") || qa_ss_is(name, "weapprev")))
    return false;
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(sim, actor);
  qa_source_item_named_entry owned[SS_UI_ITEMS]; size_t owned_count = 0;
  for (size_t i = 0; i < ui.item_count && owned_count < SS_UI_ITEMS; i++)
    if (qa_ss_is(ui.items[i].kind, "weapon") && ui.items[i].owned) owned[owned_count++] = (qa_source_item_named_entry){ui.items[i].id, ui.items[i].label};
  if (sim->strategy->forwards_commands(sim) && qa_ss_is(name, "use")) return false;
  bool named = qa_ss_is(name, "use") || qa_ss_is(name, "weapon");
  const qa_source_item_named_entry *selected = NULL;
  if (named) {
    char text[1024]; ss_join(args, arg_count, text, sizeof text);
    size_t indices[SS_UI_ITEMS];
    qa_source_item_named_result choice = qa_source_item_named(owned, owned_count, text, NULL, indices, SS_UI_ITEMS);
    if (choice.kind == QA_SOURCE_ITEM_NAMED_AMBIGUOUS) { ss_print_ambiguous(sim, actor, "weapon", owned, indices, choice.match_count); return true; }
    if (choice.kind == QA_SOURCE_ITEM_NAMED_MATCH) selected = &owned[choice.item_index];
  } else if (owned_count != 0) {
    /* owned[(owned.findIndex(item => item.id === ui.activeWeapon) + (next ? 1 : owned.length - 1)) % owned.length] */
    long index = -1;
    for (size_t i = 0; i < owned_count; i++) if (ui.active_weapon != NULL && qa_ss_is(owned[i].item, ui.active_weapon)) { index = (long)i; break; }
    long step = qa_ss_is(name, "weapnext") ? 1 : (long)owned_count - 1;
    selected = &owned[(size_t)((index + step) % (long)owned_count)];
  }
  if (selected != NULL) {
    qa_provider_id provider;
    if (!qa_shared_inventory_table_item_owner(sim->inventory, actor, selected->item, &provider)) provider = sim->weapon_provider.provider;
    qa_app_shared_sim_request_weapon(sim, actor, (qa_weapon_reference){provider, selected->item});
  }
  return selected != NULL || qa_ss_is(name, "weapnext") || qa_ss_is(name, "weapprev");
}

/* ---- sourceItemCommand (7778-7801) ---- */
bool qa_app_shared_sim_source_item_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name,
    const char *const *args, size_t arg_count) {
  if (sim->strategy->forwards_commands(sim) || (!qa_ss_is(name, "use") && !qa_ss_is(name, "drop"))) return false;
  qa_owned_actor owner; bool owned = ss_owned(sim, actor, &owner);
  qa_app_q3_source_inventory_row equipment[64]; size_t equipment_count = 0;
  if (owned && sim->selected_q3_source != NULL) {
    equipment_count = qa_app_q3_selected_source_inventory(sim->selected_q3_source, owner, equipment, 64);
    if (equipment_count > 64) equipment_count = 64;
  }
  size_t definition_count = 0;
  const qa_source_item_definition *definitions = qa_shared_inventory_table_item_definitions(sim->inventory, actor, &definition_count);
  if (definition_count == 0 && equipment_count == 0) return false;
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(sim, actor);
  /* [...new Map([...primary, ...equipment, ...definitions].map(item => [item.item, item])).values()] */
  qa_source_item_named_entry rows[2 * SS_UI_ITEMS + 64]; size_t row_count = 0;
  const size_t capacity = sizeof rows / sizeof rows[0];
  for (size_t pass = 0; pass < 3; pass++) {
    size_t count = pass == 0 ? ui.item_count : pass == 1 ? equipment_count : definition_count;
    for (size_t i = 0; i < count; i++) {
      qa_source_item_named_entry row = pass == 0 ? (qa_source_item_named_entry){ui.items[i].id, ui.items[i].label}
          : pass == 1 ? (qa_source_item_named_entry){equipment[i].item, equipment[i].label} : (qa_source_item_named_entry){definitions[i].item, definitions[i].label};
      size_t at = row_count;
      for (size_t j = 0; j < row_count; j++) if (qa_ss_is(rows[j].item, row.item)) { at = j; break; }
      if (at == row_count) { if (row_count == capacity) ss_fail("Source item rows exceed the command capacity"); row_count++; }
      rows[at] = row;
    }
  }
  char text[1024]; ss_join(args, arg_count, text, sizeof text);
  size_t indices[2 * SS_UI_ITEMS + 64];
  qa_source_item_named_result choice = qa_source_item_named(rows, row_count, text, NULL, indices, capacity);
  if (choice.kind == QA_SOURCE_ITEM_NAMED_AMBIGUOUS) { ss_print_ambiguous(sim, actor, "item", rows, indices, choice.match_count); return true; }
  if (choice.kind != QA_SOURCE_ITEM_NAMED_MATCH) return false;
  const qa_source_item_named_entry *item = &rows[choice.item_index];
  qa_source_item_action_invocation action = qa_shared_inventory_table_item_action(sim->inventory, actor, item->item,
      qa_ss_is(name, "use") ? QA_SOURCE_ITEM_ACTION_USE : QA_SOURCE_ITEM_ACTION_DROP);
  if (action.has_value) { action.call(action.ctx); return true; }
  const qa_source_item_definition *definition = ss_item_definition(sim, actor, item->item);
  if (definition != NULL && definition->is_weapon && qa_ss_is(name, "use")) {
    qa_app_shared_sim_request_weapon(sim, actor, (qa_weapon_reference){definition->source.provider, definition->item});
    return true;
  }
  if (qa_ss_is(name, "use") && ss_family_is(sim, "q3") && qa_app_q3_selected_arsenal_use_item(sim->selected_arsenal.q3, actor, item->item)) return true;
  bool in_equipment = false;
  for (size_t i = 0; i < equipment_count; i++) if (qa_ss_is(equipment[i].item, item->item)) { in_equipment = true; break; }
  if (definition == NULL && !in_equipment) return false;
  char message[512];
  snprintf(message, sizeof message, "%s cannot be %s.\n", item->label, qa_ss_is(name, "use") ? "used" : "dropped");
  ss_print_to(sim, actor, message);
  return true;
}

/* ---- nativeDropAmmoSource (5758-5768) ---- */
qa_item_id qa_app_shared_sim_native_drop_ammo_source(qa_app_shared_simulation *sim, qa_item_id item) {
  const qa_pickup_supply_profile *supply = sim->selected_original_supply;
  qa_compat_q2_native_primary_commands *commands = sim->native_primary_commands;
  if (supply == NULL || commands == NULL) ss_fail("Selected dropped ammunition has no original supply policy");
  size_t slot_count = 0;
  const qa_compat_q2_native_inventory_slot *slots = qa_compat_q2_native_primary_commands_inventory_slots(commands, &slot_count);
  bool native_ammo = false;
  for (size_t i = 0; i < slot_count; i++) if (qa_ss_is(slots[i].item, item) && slots[i].ammunition) { native_ammo = true; break; }
  if (native_ammo) for (size_t i = 0; i < supply->ammo_count; i++) {
    if (!qa_ss_is(supply->ammo[i].source, item)) continue;
    for (size_t j = 0; j < supply->ammo[i].destination_count; j++) if (qa_ss_is(supply->ammo[i].destinations[j], item)) return item;
  }
  qa_item_id owner = NULL;
  for (size_t i = 0; i < supply->ammo_owner_count; i++) if (qa_ss_is(supply->ammo_owners[i].item, item)) { owner = supply->ammo_owners[i].source; break; }
  const qa_pickup_supply_mapping *mapping = NULL; size_t candidates = 0;
  for (size_t i = 0; i < supply->ammo_count; i++) {
    bool includes = false;
    for (size_t j = 0; j < supply->ammo[i].destination_count; j++) if (qa_ss_is(supply->ammo[i].destinations[j], item)) includes = true;
    if (!includes) continue;
    candidates++;
    if (owner == NULL ? candidates == 1 : (mapping == NULL && qa_ss_is(supply->ammo[i].source, owner))) mapping = &supply->ammo[i];
  }
  if (owner == NULL && candidates != 1) mapping = NULL;
  bool available = false;
  if (mapping != NULL) for (size_t i = 0; i < slot_count; i++) if (qa_ss_is(slots[i].item, mapping->source) && slots[i].ammunition) { available = true; break; }
  if (mapping == NULL || !available) ss_fail("Selected ammunition %s has no unique available original drop owner", item);
  return mapping->source;
}

/* ---- giveSelectedItem (7714-7758) ---- */
/* value.toLowerCase().replaceAll(" ", "").replaceAll("_", "") */
static void ss_normalize(const char *value, char *out, size_t capacity) {
  size_t length = 0;
  for (const char *c = value; *c != '\0' && length + 1 < capacity; c++) {
    if (*c == ' ' || *c == '_') continue;
    out[length++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
  }
  out[length] = '\0';
}
static bool ss_normalized_equal(const char *a, const char *b) {
  char left[256], right[256]; ss_normalize(a, left, sizeof left); ss_normalize(b, right, sizeof right);
  return strcmp(left, right) == 0;
}
static bool ss_q1_registered(qa_q1_services *game, qa_q1_weapon weapon) {
  for (size_t i = 0; i < game->registered_weapon_count; i++) if (qa_ss_is(game->registered_weapons[i].def->id, weapon)) return true;
  return false;
}
static __thread ss_ui_storage ss_give_ui;
/* SourceStrategy.grantProfile (Q1, Q2, Q3) over this.selectedArsenal. */
const qa_pickup_supply_profile *qa_ss_q1_grant_profile(qa_app_shared_simulation *sim) {
  return ss_family_is(sim, "q3") ? &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE
      : ss_family_is(sim, "q2") ? &QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE : &QA_COMPOSITION_Q1_HIPNOTIC_SUPPLY_PROFILE;
}
const qa_pickup_supply_profile *qa_ss_q2_grant_profile(qa_app_shared_simulation *sim) {
  bool hipnotic = ss_family_is(sim, "q1") && ss_q1_registered(qa_app_shared_sim_selected_q1_game(sim), "hipnotic:laser");
  return ss_family_is(sim, "q3") ? &QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE
      : hipnotic ? &QA_COMPOSITION_Q2_HIPNOTIC_SUPPLY_PROFILE : &QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE;
}
const qa_pickup_supply_profile *qa_ss_q3_grant_profile(qa_app_shared_simulation *sim) {
  bool hipnotic = ss_family_is(sim, "q1") && ss_q1_registered(qa_app_shared_sim_selected_q1_game(sim), "hipnotic:laser");
  return ss_family_is(sim, "q2") ? &QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE
      : hipnotic ? &QA_COMPOSITION_Q3_HIPNOTIC_SUPPLY_PROFILE : &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE;
}
/* Q2Strategy.grantedWeapon */
void qa_ss_q2_granted_weapon(qa_app_shared_simulation *sim, qa_owned_actor owner, const char *requested, const qa_item_id *destinations,
    size_t destination_count, const qa_pickup_supply_profile *profile) {
  const qa_q2w_definition *definition = ss_q2_definition(sim->source.q2_weapons, requested);
  qa_q2_inventory_item source_ammo;
  bool has_source_ammo = definition != NULL && definition->ammo != NULL && qa_q2_item_module_lookup(sim->source.product->items, definition->ammo, &source_ammo);
  const qa_pickup_supply_mapping *mapped_ammo = NULL;
  if (has_source_ammo && profile != NULL)
    for (size_t i = 0; i < profile->ammo_count && mapped_ammo == NULL; i++) if (qa_ss_is(profile->ammo[i].source, source_ammo.id)) mapped_ammo = &profile->ammo[i];
  if (mapped_ammo != NULL && has_source_ammo) {
    bool infinite = ((int64_t)qa_q2_options_deathmatch_flags(&sim->source.product->services->options) & 8192) != 0;
    for (size_t i = 0; i < mapped_ammo->destination_count; i++)
      qa_shared_inventory_table_give(sim->inventory, owner, mapped_ammo->destinations[i], infinite ? 1000 : source_ammo.quantity);
  }
  ss_sel_pickup_weapons(sim, owner, destinations, destination_count, QA_PICKUP_SELECTION_BETTER);
}
bool qa_app_shared_sim_give_selected_item(qa_app_shared_simulation *sim, qa_actor_id actor, const char *const *args, size_t arg_count) {
  if (!sim->has_selected_arsenal) return false;
  qa_owned_actor owner;
  if (!ss_owned(sim, actor, &owner)) ss_fail("Selected grant actor is retired");
  /* last !== undefined && /^-?\d+$/.test(last) ? Number(last) : null */
  bool has_quantity = false; double quantity = 0;
  if (arg_count != 0) {
    const char *last = args[arg_count - 1], *digits = last[0] == '-' ? last + 1 : last;
    bool numeric = digits[0] != '\0';
    for (const char *c = digits; *c != '\0'; c++) if (*c < '0' || *c > '9') numeric = false;
    if (numeric) { has_quantity = true; quantity = qa_js_string_to_number(last); }
  }
  char requested[1024]; ss_join(args, has_quantity ? arg_count - 1 : arg_count, requested, sizeof requested);
  for (char *c = requested; *c != '\0'; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
  if (sim->selected_q3_source != NULL && qa_app_q3_selected_source_give_holdable(sim->selected_q3_source, owner, requested)) return true;
  qa_arsenal_state state = ss_sel_read(sim, actor);
  qa_app_arsenal_ui ui = ss_sel_ui(sim, actor, sim->recipe.map.entities, &ss_give_ui);
  const qa_app_player_ui_item *named = NULL;
  for (size_t i = 0; i < ui.item_count; i++)
    if (ss_normalized_equal(ui.items[i].id, requested) || (ui.items[i].label != NULL && ss_normalized_equal(ui.items[i].label, requested))) { named = &ui.items[i]; break; }
  const qa_inventory_entry *entry = NULL;
  for (size_t i = 0; i < state.ammo_count && entry == NULL; i++) {
    const char *slash = strrchr(state.ammo[i].item, '/');
    if ((named != NULL && qa_ss_is(state.ammo[i].item, named->id)) || ss_normalized_equal(state.ammo[i].item, requested)
        || ss_normalized_equal(slash ? slash + 1 : state.ammo[i].item, requested)) entry = &state.ammo[i];
  }
  if (entry == NULL) {
    ss_arsenal_free(&state);
    const qa_pickup_supply_profile *profile = sim->strategy->grant_profile(sim);
    const qa_pickup_supply_mapping *weapon = NULL, *ammo = NULL;
    if (profile != NULL) {
      for (size_t i = 0; i < profile->weapon_count && weapon == NULL; i++) if (qa_ss_is(profile->weapons[i].source, requested)) weapon = &profile->weapons[i];
      for (size_t i = 0; i < profile->ammo_count && ammo == NULL; i++) if (qa_ss_is(profile->ammo[i].source, requested)) ammo = &profile->ammo[i];
    }
    qa_q2_inventory_item source_item; bool has_source_item = sim->strategy->source_item(sim, requested, &source_item);
    const qa_pickup_supply_mapping *destinations = weapon != NULL ? weapon : ammo;
    if (destinations == NULL) {
      if ((has_source_item && (source_item.weapon || qa_ss_is(source_item.kind, "ammo"))) || strncmp(requested, "q1:weapon/", 10) == 0
          || strncmp(requested, "q1:ammo/", 8) == 0 || strncmp(requested, "rogue:ammo/", 11) == 0) {
        char message[1200];
        snprintf(message, sizeof message, "No selected-arsenal grant mapping for %s\n", has_source_item ? source_item.name : requested);
        ss_print_to(sim, actor, message);
        return true;
      }
      return false;
    }
    for (size_t i = 0; i < destinations->destination_count; i++) {
      qa_item_id item = destinations->destinations[i];
      const qa_inventory_entry *target = ss_entry(sim, actor, item);
      if (target == NULL) ss_fail("Selected grant destination is unavailable: %s", item);
      double count = weapon != NULL ? 1 : has_quantity ? quantity
          : has_source_item && qa_ss_is(source_item.kind, "ammo") ? target->count + source_item.quantity : target->capacity;
      qa_inventory_entry configured = *target;
      configured.count = qa_js_max(0, count); configured.capacity = qa_js_max(target->capacity, count);
      ss_configure(sim, owner, configured);
    }
    if (weapon != NULL) sim->strategy->granted_weapon(sim, owner, requested, weapon->destinations, weapon->destination_count, profile);
    qa_app_shared_sim_refresh_arsenal_cache(sim, actor);
    return true;
  }
  bool is_weapon = false;
  for (size_t i = 0; i < ui.item_count; i++) if (qa_ss_is(ui.items[i].id, entry->item) && qa_ss_is(ui.items[i].kind, "weapon")) { is_weapon = true; break; }
  double count = is_weapon ? 1 : has_quantity ? quantity : entry->capacity;
  qa_inventory_entry configured = *entry;
  configured.count = qa_js_max(0, count); configured.capacity = qa_js_max(entry->capacity, count);
  ss_arsenal_free(&state);
  ss_configure(sim, owner, configured);
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor);
  return true;
}

/* ---- characterViews (7674-7688) ---- */
const qa_q3_character_view *qa_app_shared_sim_character_views(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_q3_character_view *storage; static __thread size_t capacity;
  size_t group_count = 0;
  const qa_session_mod_appearance_group *groups = sim->mod_owner != NULL ? qa_session_mods_appearance_overrides(sim->mod_owner, &group_count) : NULL;
  size_t player_count = 0, count = 0;
  qa_app_sim_movement_player *const *players = qa_app_shared_sim_presentation_players(sim, &player_count);
  storage = ss_grow(storage, &capacity, player_count + 1, sizeof *storage);
  bool native_q3 = sim->strategy->renders_own_intermission(sim);
  for (size_t i = 0; i < player_count; i++) {
    qa_app_sim_movement_player *player = players[i];
    qa_owned_actor owner = player->actor;
    qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &owner);
    qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, owner.id, &body);
    qa_animation_state animation = character ? qa_q3_character_actor_get_animation(*character).state : player->animation.state;
    bool overridden = false;
    for (size_t j = 0; j < group_count; j++) if (qa_same_actor(groups[j].actor, owner.id)) { overridden = true; break; }
    if (overridden || player->character != QA_GAME_FAMILY_Q3 || (native_q3 && (player->intermission || player->has_cutscene))
        || !has_body || !qa_ss_is(animation.kind, "q3")) continue;
    qa_q1_player_state *q1_player = sim->strategy->q1_player(sim, owner.id);
    qa_ss_visual_fields visual = q1_player == NULL ? (qa_ss_visual_fields){1, 1} : qa_app_shared_sim_q1_visual_fields(q1_player->alpha, q1_player->scale);
    qa_q3_character_view *view = &storage[count++];
    memset(view, 0, sizeof *view);
    view->actor = owner.id; view->origin = body.origin; view->angles = player->view_angles; view->velocity = body.velocity;
    view->movement_direction = player->state.kind == QA_MOVEMENT_STATE_Q3 ? player->state.as.q3.movement_direction : 0;
    view->animation = animation;
    view->source_flags = character ? qa_q3_character_actor_get_source_flags(*character) : ss_health_or(sim, owner.id, 0) <= 0 ? 1 : 0;
    view->powerups = 0; view->team = NULL;
    view->has_scale = true; view->scale = visual.scale; view->has_opacity = true; view->opacity = visual.alpha;
    view->color = (qa_vec4){1, 1, 1, 1};
  }
  *out_count = count;
  return storage;
}

/* ---- player->arsenal ownership: a selected read's ammo array is malloc'd;
 * the one a player holds is adopted here and freed when replaced. ---- */
void qa_app_shared_sim_store_player_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_arsenal_state arsenal) {
  if (sim->b2_player_ammo.value_size == 0) qa_ss_map_init(&sim->b2_player_ammo, QA_SS_KEY_ACTOR, sizeof(qa_inventory_entry *));
  qa_actor_id actor = player->actor.id;
  qa_inventory_entry **kept = qa_ss_map_get(&sim->b2_player_ammo, &actor);
  qa_inventory_entry *ammo = (qa_inventory_entry *)arsenal.ammo;
  if (kept != NULL && *kept != ammo) free(*kept);
  qa_ss_map_set(&sim->b2_player_ammo, &actor, &ammo);
  player->arsenal = arsenal;
}

/* ======== B2 rows ported by runtime-b2b ======== */

/* this.selectedOriginalWeapons.get(item): has false = absent (TS undefined). */
static const qa_ss_optional_item *ss_original_weapon(qa_app_shared_simulation *sim, qa_item_id item) {
  return sim->has_selected_original_weapons ? qa_ss_map_get(&sim->selected_original_weapons, &item) : NULL;
}
/* nativePrimaryWeaponHost(this.source.game, this.actors): the context is
 * simulation storage, refilled per call (the TS hosts are transient). */
static qa_compat_q2_native_primary_weapon_host ss_native_host(qa_app_shared_simulation *sim) {
  qa_app_sim_q2_native_world world = {!ss_native_classic(sim), sim->source.classic_world, sim->source.rerelease_world};
  return qa_app_sim_native_primary_weapon_host(&sim->b2_native_host, world, sim->actors);
}

/* The B2 views bindNativeInput (shared_simulation_worlds.c) reads. */
qa_compat_q2_native_primary_weapon_host qa_app_shared_sim_native_primary_weapon_host(qa_app_shared_simulation *sim) { return ss_native_host(sim); }
const qa_selected_pickup_weapon *qa_app_shared_sim_selected_arsenal_catalog(qa_app_shared_simulation *sim, size_t *out_count) {
  return ss_sel_catalog(sim, out_count);
}
/* selectedArsenal.ui(actor, this.weaponProvider) as the Pick<PlayerUi> it returns. */
qa_app_sim_player_ui qa_app_shared_sim_selected_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_player_ui_storage *storage = ss_ui_slot();
  qa_app_arsenal_ui arsenal = ss_sel_ui(sim, actor, sim->weapon_provider, &storage->arsenal);
  qa_app_sim_player_ui ui; memset(&ui, 0, sizeof ui);
  ss_spread_arsenal_ui(&ui, &arsenal, storage);
  return ui;
}

/* ---- selectedQ3Supply (2021-2041) ---- */
typedef struct ss_q3_supply {
  const char *kind; /* "native" | "rejected" | "selected" */
  qa_pickup_supply_offer offer; qa_pickup_ammo_grant grant; double respawn_seconds;
} ss_q3_supply;
static void ss_selected_q3_supply(qa_app_shared_simulation *sim, const qa_q3_source_pickup_descriptor *pickup, ss_q3_supply *out) {
  memset(out, 0, sizeof *out);
  out->kind = "native";
  if (!sim->has_selected_arsenal || qa_ss_is(sim->selected_arsenal.family, "q3") || sim->selected_supply == NULL) return;
  if (pickup->item->type != QA_Q3_BASE_IT_WEAPON && pickup->item->type != QA_Q3_BASE_IT_AMMO) return;
  out->kind = "rejected";
  const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(pickup->item->tag);
  if (weapon == NULL || !qa_session_actor_registry_is_live(sim->actors, pickup->player_actor)) return;
  if (pickup->item->type == QA_Q3_BASE_IT_AMMO) {
    if (weapon->ammo == NULL) return;
    out->kind = "selected"; out->offer.kind = QA_PICKUP_SUPPLY_AMMO;
    out->offer.grant.item = weapon->ammo; out->offer.grant.amount = pickup->count != 0 ? pickup->count : pickup->item->quantity;
    out->respawn_seconds = QA_Q3_RESPAWN_AMMO;
    return;
  }
  qa_composition_owned_profile owned;
  if (!qa_composition_expansion_source_supply(ss_family_is(sim, "q1") ? &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE
          : &QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE, &owned)) qa_range_error("Out of memory");
  qa_item_id destination = NULL;
  if (weapon->ammo != NULL)
    for (size_t i = 0; i < owned.ammo_count; i++) if (qa_ss_is(owned.ammo[i].source, weapon->ammo)) {
      destination = owned.ammo[i].destination_count != 0 ? owned.ammo[i].destinations[0] : NULL;
      break;
    }
  qa_composition_owned_profile_destroy(&owned);
  if (weapon->ammo != NULL && destination == NULL) return;
  qa_q3_weapon_pickup_quantity_input quantity = {pickup->count, pickup->item->quantity, pickup->dropped, pickup->game_type,
      destination == NULL ? 0 : ss_count(sim, pickup->player_actor, destination)};
  out->kind = "selected"; out->offer.kind = QA_PICKUP_SUPPLY_WEAPON; out->offer.item = weapon->item;
  out->grant.item = weapon->ammo; out->grant.amount = qa_q3_weapon_pickup_quantity(quantity);
  out->offer.ammo = weapon->ammo == NULL ? NULL : &out->grant; out->offer.ammo_count = weapon->ammo == NULL ? 0 : 1;
  qa_q3_weapon_pickup_context respawn = {pickup->game_type, pickup->weapon_respawn_seconds, pickup->team_weapon_respawn_seconds};
  out->respawn_seconds = qa_q3_weapon_respawn_seconds(respawn);
}
/* ---- previewSelectedQ3Pickup (2043-2049): the offer's ammo grant is
 * thread-local storage valid until the next call; the preview receipt
 * arrays are malloc'd (qa_free_pickup_supply_preview). ---- */
qa_q3_source_pickup_preview qa_app_shared_sim_preview_selected_q3_pickup(qa_app_shared_simulation *sim, qa_q3_source_pickup_descriptor pickup) {
  static __thread ss_q3_supply supplied;
  ss_selected_q3_supply(sim, &pickup, &supplied);
  qa_q3_source_pickup_preview result; memset(&result, 0, sizeof result);
  if (!qa_ss_is(supplied.kind, "selected")) {
    result.kind = qa_ss_is(supplied.kind, "native") ? QA_Q3_SOURCE_PREVIEW_NATIVE : QA_Q3_SOURCE_PREVIEW_REJECTED;
    return result;
  }
  if (sim->selected_supply == NULL) ss_fail("Selected source supply lost its admission");
  result.kind = QA_Q3_SOURCE_PREVIEW_SELECTED; result.offer = supplied.offer;
  result.preview = qa_shared_pickup_admission_preview(sim->selected_supply, pickup.player_actor, supplied.offer);
  return result;
}
/* ---- admitSelectedQ3Pickup (2051-2062) ---- */
qa_q3_source_pickup_admission qa_app_shared_sim_admit_selected_q3_pickup(qa_app_shared_simulation *sim, qa_q3_source_pickup_descriptor pickup) {
  if (sim->selected_q3_source != NULL) {
    qa_q3_source_pickup_admission equipment = qa_app_q3_selected_source_take_pickup(sim->selected_q3_source, pickup);
    if (equipment.kind != QA_Q3_SOURCE_PICKUP_NATIVE) return equipment;
  }
  ss_q3_supply supplied;
  ss_selected_q3_supply(sim, &pickup, &supplied);
  qa_q3_source_pickup_admission result = {QA_Q3_SOURCE_PICKUP_REJECTED, 0};
  if (!qa_ss_is(supplied.kind, "selected")) {
    if (qa_ss_is(supplied.kind, "native")) result.kind = QA_Q3_SOURCE_PICKUP_NATIVE;
    return result;
  }
  qa_shared_pickup_admission *admission = sim->selected_supply;
  qa_owned_actor actor;
  bool owned = ss_owned(sim, pickup.player_actor, &actor);
  if (admission == NULL || !owned) return result;
  bool accepted;
  if (supplied.offer.kind == QA_PICKUP_SUPPLY_AMMO)
    accepted = qa_shared_pickup_admission_ammo(admission, actor, supplied.offer.grant, true, NULL, NULL);
  else {
    qa_pickup_weapon_offer offer = {supplied.offer.item, supplied.offer.ammo, supplied.offer.ammo_count};
    accepted = supplied.offer.kind == QA_PICKUP_SUPPLY_WEAPON && qa_shared_pickup_admission_weapon(admission, actor, offer,
        qa_ss_is(sim->options.mode, "deathmatch") ? QA_PICKUP_SELECTION_BETTER : QA_PICKUP_SELECTION_ALWAYS, NULL, NULL);
  }
  if (accepted) { result.kind = QA_Q3_SOURCE_PICKUP_PICKED; result.respawn_seconds = supplied.respawn_seconds; }
  return result;
}

/* ---- selectedNativePlayer (2461-2466) ---- */
qa_compat_q2_native_primary_player qa_app_shared_sim_selected_native_player(qa_app_shared_simulation *sim) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE)) ss_fail("Selected native equipment requires its source player");
  const qa_compat_q2_native_primary_profile *profile = qa_app_shared_sim_native_primary(sim);
  const qa_compat_q2_native_primary_weapon_profile *weapon = profile != NULL ? profile->weapons : NULL;
  const qa_compat_q2_native_primary_player_profile *player = profile != NULL ? profile->player : NULL;
  if (weapon == NULL || player == NULL) ss_fail("Selected equipment requires qualified original native player services");
  qa_compat_q2_native_primary_player result;
  qa_compat_q2_native_primary_player_init(&result, ss_native_host(sim), weapon, player);
  return result;
}

/* ---- selectedQ3Spawn (2468-2480): SourceStrategy.teleportSpawn per class ---- */
void qa_app_shared_sim_selected_q3_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  sim->strategy->teleport_spawn(sim, actor, out_origin, out_angles);
}
void qa_ss_loading_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  (void)sim; (void)actor; (void)out_origin; (void)out_angles;
  ss_fail("Selected map travel is unavailable while the world is loading");
}
void qa_ss_q1_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_q1_actor *spot = qa_comp_q1_source_composition_select_spawn(sim->source.q1_composition, &actor.id, true);
  if (spot == NULL) ss_fail("Selected teleporter has no source spawn");
  qa_q1_body body = qa_q1_get_body(sim->source.q1_game, spot);
  *out_origin = body.origin; *out_angles = body.angles;
}
void qa_ss_quakec_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_app_sim_quakec_spawn_point point = qa_app_sim_quakec_source_client_spawn_point(sim->source.quakec, actor.id);
  *out_origin = point.origin; *out_angles = point.angles;
}
void qa_ss_q2_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_entity *entity = game->entity(game->ctx, &actor.id);
  if (entity == NULL) ss_fail("Selected teleporter has no Q2 player");
  qa_q2_spawn_placement placement = qa_q2_players_select_teleport_spawn(sim->source.product->players, entity, game);
  *out_origin = placement.origin; *out_angles = placement.angles;
}
void qa_ss_q2_native_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_compat_q2_native_primary_player player = qa_app_shared_sim_selected_native_player(sim);
  qa_compat_q2_native_spawn_point point = qa_compat_q2_native_primary_player_spawn_point(&player, actor.id);
  *out_origin = point.origin; *out_angles = point.angles;
}
void qa_ss_q3_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) ss_fail("Selected teleporter lost its body");
  qa_q3_ta_spawn_pose pose = qa_app_q3_source_runtime_select_spawn_point(sim->source.q3, body.origin);
  *out_origin = pose.origin; *out_angles = pose.angles;
}
void qa_ss_q3_qvm_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_qvm_primary_weapons_spawn_point(qa_app_shared_sim_require_qvm_weapons(sim), actor.id, out_origin, out_angles);
}

/* ---- dropSelectedQ3Objectives (2482-2496): SourceStrategy.dropObjectives per class ---- */
static qa_q3_game_entity *ss_q3_drop_item(void *ctx, qa_q3_game_entity *entity, const qa_q3_base_item_definition *item, double angle) {
  return qa_app_q3_source_runtime_drop_item(ctx, entity, item, angle);
}
void qa_app_shared_sim_drop_selected_q3_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  sim->strategy->drop_objectives(sim, actor);
}
void qa_ss_loading_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  (void)sim; (void)actor;
  ss_fail("Selected objective drop has no source map owner");
}
void qa_ss_q1_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  if (sim->source.q1_composition->ctf != NULL) qa_q1ad_ctf_drop_flag(sim->source.q1_composition->ctf, actor.id);
  if (sim->source.q1_composition->packs != NULL) qa_q1mp_world_drop_carried_flag(&sim->source.q1_composition->packs->world, actor.id);
}
void qa_ss_quakec_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_app_sim_quakec_source_drop_client_objectives(sim->source.quakec, actor.id);
}
void qa_ss_q2_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_q2_product_runtime *product = sim->source.product;
  qa_q2_game_services *game = product->services;
  qa_q2_entity *entity = game->entity(game->ctx, &actor.id);
  if (entity != NULL && product->match.source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_flags_drop(qa_q2_ctf_get_flags(product->match.ctf), entity, game);
  else if (entity != NULL && product->match.source_kind == QA_Q2_MATCH_LMCTF) qa_lmctf_flags_drop(qa_q2_lmctf_get_flags(product->match.lmctf), entity, game);
}
void qa_ss_q2_native_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_compat_q2_native_primary_player player = qa_app_shared_sim_selected_native_player(sim);
  qa_compat_q2_native_primary_player_drop_objectives(&player, actor.id);
}
void qa_ss_q3_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor.id);
  if (entity == NULL) ss_fail("Selected objective drop has no map player");
  qa_q3_ta_teleport_objectives_context context;
  context.product = qa_ss_is(qa_app_q3_source_runtime_options(sim->source.q3)->product, "missionpack") ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  context.combat = qa_q3_base_combat_bridge_context(qa_app_q3_source_runtime_bridge(sim->source.q3));
  context.ctx = sim->source.q3; context.drop_item = ss_q3_drop_item;
  qa_q3_ta_drop_teleport_objectives(&context, entity);
}
void qa_ss_q3_qvm_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_qvm_primary_weapons_drop_objectives(qa_app_shared_sim_require_qvm_weapons(sim), actor.id);
}

/* SourceStrategy.originalPlayer (Q2Native, Q3Qvm) and setMaxHealth per class. */
static void ss_native_original_max_health(qa_app_shared_simulation *sim, const qa_app_source_original_player *self, qa_actor_id actor, double health) {
  (void)sim; qa_compat_q2_native_primary_player_set_max_health(self->state, actor, health);
}
static void ss_native_original_teleport(qa_app_shared_simulation *sim, const qa_app_source_original_player *self, qa_actor_id actor,
    qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, double hold) {
  (void)sim; qa_compat_q2_native_primary_player_teleport(self->state, actor, origin, velocity, angles, hold);
}
bool qa_ss_q2_native_original_player(qa_app_shared_simulation *sim, qa_app_source_original_player *out) {
  static __thread qa_compat_q2_native_primary_player player;
  player = qa_app_shared_sim_selected_native_player(sim);
  out->state = &player; out->set_max_health = ss_native_original_max_health; out->teleport = ss_native_original_teleport;
  return true;
}
static void ss_qvm_original_max_health(qa_app_shared_simulation *sim, const qa_app_source_original_player *self, qa_actor_id actor, double health) {
  (void)sim; qa_qvm_primary_weapons_set_max_health(self->state, actor, health);
}
static void ss_qvm_original_teleport(qa_app_shared_simulation *sim, const qa_app_source_original_player *self, qa_actor_id actor,
    qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, double hold) {
  (void)sim; qa_qvm_primary_weapons_teleport_state(self->state, actor, origin, velocity, angles, hold);
}
bool qa_ss_q3_qvm_original_player(qa_app_shared_simulation *sim, qa_app_source_original_player *out) {
  out->state = qa_app_shared_sim_require_qvm_weapons(sim); out->set_max_health = ss_qvm_original_max_health; out->teleport = ss_qvm_original_teleport;
  return true;
}
void qa_ss_q1_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  qa_q1_player_state *target = qa_q1_player(sim->source.q1_game, actor);
  if (target == NULL) ss_fail("Selected max health has no Q1 player");
  target->max_health = health;
}
void qa_ss_quakec_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  qa_app_sim_quakec_source_set_client_max_health(sim->source.quakec, actor, health);
}
void qa_ss_q2_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_entity *target = game->entity(game->ctx, &actor);
  if (target == NULL) ss_fail("Selected max health has no Q2 player");
  target->max_health = health;
}
void qa_ss_q3_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  qa_q3_game_client *target = ss_q3_native_client(sim, actor);
  if (target == NULL) ss_fail("Selected max health has no Q3 client");
  target->pers.max_health = health;
  qa_q3_base_player_state_slots_set(&target->ps.stats, qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(&target->ps))->max_health, health);
}
void qa_ss_q3_qvm_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  qa_qvm_primary_weapons_set_max_health(qa_app_shared_sim_require_qvm_weapons(sim), actor, health);
}

/* ---- selectedQ3ClientChanged (2498-2539) ---- */
static void ss_view_reset_event(qa_app_shared_simulation *sim, const char *reason, qa_actor_id actor, qa_vec3 angles) {
  qa_app_sim_source_event_input event; memset(&event, 0, sizeof event);
  event.kind = "view-reset";
  event.view_reset = (qa_app_sim_view_reset_event){reason, actor, angles};
  qa_app_shared_sim_emit(sim, sim->weapon_provider.content, event, NULL, NULL);
}
/* ((after - before + 32768 & 65535) - 32768) */
static double ss_word_delta(double after, double before) { return (double)((qa_js_to_int32(after - before + 32768) & 65535) - 32768); }
void qa_app_shared_sim_selected_q3_client_changed(qa_app_shared_simulation *sim, qa_owned_actor actor,
    qa_app_q3_selected_client_effects before, qa_app_q3_selected_client_effects after) {
  qa_app_source_original_player original;
  if (sim->strategy->original_player(sim, &original)) {
    if (before.max_health != after.max_health) original.set_max_health(sim, &original, actor.id, after.max_health);
    if (before.teleport_bit != after.teleport_bit) {
      qa_body_state body;
      if (!qa_app_shared_sim_body(sim, actor.id, &body)) ss_fail("Selected teleport lost its original player body");
      original.teleport(sim, &original, actor.id, body.origin, body.velocity, after.view_angles, after.pm_time);
      if (sim->grapple != NULL) qa_app_sim_grapple_runtime_release(sim->grapple, actor.id);
      if (sim->selected_q3_source != NULL) qa_app_q3_selected_source_release_hook(sim->selected_q3_source, actor.id);
      ss_view_reset_event(sim, "teleport", actor.id, after.view_angles);
    }
    return;
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player == NULL) return;
  if (before.max_health != after.max_health) sim->strategy->set_max_health(sim, actor.id, after.max_health);
  bool changed_angles = before.view_angles.x != after.view_angles.x || before.view_angles.y != after.view_angles.y
      || before.view_angles.z != after.view_angles.z;
  if (before.teleport_bit != after.teleport_bit) {
    qa_body_state body;
    if (!qa_app_shared_sim_body(sim, actor.id, &body)) ss_fail("Selected Q3 teleport lost its destination body");
    qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
    change.kind = "teleport"; change.origin = body.origin; change.angles = after.view_angles; change.velocity = body.velocity;
    change.hold_milliseconds = after.pm_time; change.command_angles = player->command_angles; change.spectator = false;
    qa_app_shared_sim_set_player_movement(sim, actor.id, change);
    return;
  }
  qa_vec3 delta = {ss_word_delta(after.delta_angles.x, before.delta_angles.x), ss_word_delta(after.delta_angles.y, before.delta_angles.y),
      ss_word_delta(after.delta_angles.z, before.delta_angles.z)};
  if (!changed_angles && delta.x == 0 && delta.y == 0 && delta.z == 0) return;
  qa_movement_state state = qa_app_sim_movement_player_read_state(player);
  qa_vec3 turn = {delta.x * 360 / 65536, delta.y * 360 / 65536, delta.z * 360 / 65536};
  qa_vec3 angles = changed_angles ? after.view_angles : qa_ss_add(player->view_angles, turn);
  player->view_angles = angles;
  if (state.kind == QA_MOVEMENT_STATE_Q3) {
    state.as.q3.view_angles = angles;
    state.as.q3.delta_angle_words.values[0] += delta.x; state.as.q3.delta_angle_words.values[1] += delta.y;
    state.as.q3.delta_angle_words.values[2] += delta.z;
  } else if (state.kind == QA_MOVEMENT_STATE_Q2) {
    state.as.q2.delta_angle_shorts.values[0] += delta.x; state.as.q2.delta_angle_shorts.values[1] += delta.y;
    state.as.q2.delta_angle_shorts.values[2] += delta.z;
  } else if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) state.as.q2_rerelease.delta_angles = qa_ss_add(state.as.q2_rerelease.delta_angles, turn);
  else if (state.kind == QA_MOVEMENT_STATE_Q1) state.as.q1.view_angles = angles;
  else state.as.qw.angles = angles;
  player->state = state;
  ss_view_reset_event(sim, "source", actor.id, angles);
}

/* ---- nativeDropProtection (5746-5760): execute's result is opaque; NULL
 * is also the TS undefined of a consume whose effect did not run. ---- */
typedef struct ss_drop_protection {
  qa_app_shared_simulation *sim; qa_app_sim_rerelease_guest_world *source;
  qa_compat_q2_native_primary_weapon_host host; qa_raw_entity_view record; qa_actor_id actor;
  bool debit; void *(*execute)(void *ctx); void *ctx; void *result;
} ss_drop_protection;
static void ss_drop_current(void *ctx) {
  ss_drop_protection *call = ctx;
  qa_actor_id current;
  bool same_source = (call->sim->strategy->as_q2_native(call->sim) != NULL) && call->sim->source.rerelease_world == call->source;
  if (!same_source || !call->host.actor(call->host.ctx, call->record, &current) || !qa_same_actor(current, call->actor))
    ss_fail("Original inventory drop owner retired during its effect");
}
static void ss_drop_execute(void *ctx) { ss_drop_protection *call = ctx; call->result = call->execute(call->ctx); }
static void *ss_drop_operation(void *ctx, void *consume_ctx, void (*consume)(void *consume_ctx, void *execute_ctx, void (*execute)(void *))) {
  ss_drop_protection *call = ctx;
  if (call->debit) return call->execute(call->ctx);
  call->result = NULL;
  consume(consume_ctx, call, ss_drop_execute);
  return call->result;
}
void *qa_app_shared_sim_native_drop_protection(qa_app_shared_simulation *sim, qa_actor_id actor, const char *kind,
    void *(*execute)(void *ctx), void *ctx) {
  if (!ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE)) ss_fail("Original inventory drop lost its native source");
  if (ss_native_classic(sim)) return execute(ctx);
  ss_drop_protection call; memset(&call, 0, sizeof call);
  call.sim = sim; call.source = sim->source.rerelease_world; call.host = ss_native_host(sim); call.actor = actor;
  call.debit = qa_ss_is(kind, "debit"); call.execute = execute; call.ctx = ctx;
  if (!call.host.record_for(call.host.ctx, actor, &call.record)) ss_fail("Original inventory drop lost its source actor");
  qa_compat_q2_rerelease_q2_guest_host *host = qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_world_source(call.source));
  return qa_compat_q2_with_rerelease_primary_protection(host, call.record, QA_PROTECTION_POWERED, &call, ss_drop_current,
      ss_drop_operation, NULL);
}

/* nativePrimary()?.inventory.prototypes: NULL = TS undefined. */
static const qa_compat_q2_native_primary_inventory_profile *ss_native_prototypes(qa_app_shared_simulation *sim) {
  const qa_compat_q2_native_primary_profile *profile = qa_app_shared_sim_native_primary(sim);
  return profile != NULL ? profile->inventory : NULL;
}
static bool ss_definition_has_action(const qa_source_item_definition *definition, qa_source_item_action action) {
  if (definition->actions == NULL) return false;
  for (size_t i = 0; i < definition->action_count; i++) if (definition->actions[i] == action) return true;
  return false;
}
#define SS_Q3_INVENTORY 64
/* ---- nativeDropProjection (5771-5794) ---- */
qa_compat_q2_native_drop_projection qa_app_shared_sim_native_drop_projection(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  const qa_pickup_supply_profile *supply = sim->selected_original_supply;
  const qa_compat_q2_native_primary_inventory_profile *prototypes = ss_native_prototypes(sim);
  if (prototypes == NULL) ss_fail("Native drop has no declared original item prototypes");
  const qa_source_item_definition *component = ss_item_definition(sim, actor, item);
  qa_owned_actor owner;
  bool equipment = false;
  if (ss_owned(sim, actor, &owner) && sim->selected_q3_source != NULL) {
    static __thread qa_app_q3_source_inventory_row rows[SS_Q3_INVENTORY];
    size_t count = qa_app_q3_selected_source_inventory(sim->selected_q3_source, owner, rows, SS_Q3_INVENTORY);
    if (count > SS_Q3_INVENTORY) ss_fail("Selected Q3 inventory exceeds its projection storage");
    for (size_t i = 0; i < count && !equipment; i++) equipment = qa_ss_is(rows[i].item, item);
  }
  qa_compat_q2_native_drop_projection result = {NULL, NULL, NULL};
  if (component != NULL || equipment) {
    result.source = component != NULL && ss_definition_has_action(component, QA_SOURCE_ITEM_ACTION_DROP)
        ? prototypes->proto_droppable : prototypes->proto_undroppable;
    return result;
  }
  if (!ss_sel_has(sim, actor) || supply == NULL || !sim->has_selected_original_weapons) ss_fail("Selected drop has no admitted supply policy");
  bool weapon = ss_sel_catalog_has(sim, item);
  const qa_ss_optional_item *source_weapon = ss_original_weapon(sim, item);
  qa_item_id source;
  if (weapon) {
    if (source_weapon == NULL) ss_fail("Selected dropped weapon has no original supply owner");
    source = source_weapon->has ? source_weapon->item : prototypes->proto_undroppable;
  } else source = qa_app_shared_sim_native_drop_ammo_source(sim, item);
  qa_app_sim_weapon_slot_projection slot = qa_app_shared_sim_weapon_slot(sim, actor);
  qa_item_id current[2] = {slot.active != NULL ? slot.active->item : NULL, slot.pending != NULL ? slot.pending->item : NULL};
  qa_item_id projected[2] = {NULL, NULL};
  for (int i = 0; i < 2; i++) {
    if (current[i] == NULL) continue;
    const qa_ss_optional_item *native = ss_original_weapon(sim, current[i]);
    if (native == NULL) ss_fail("Current selected weapon has no original supply owner");
    qa_item_id value = native->has ? native->item : NULL;
    projected[i] = weapon && !qa_ss_is(current[i], item) && value != NULL && qa_ss_is(value, source) ? NULL : value;
  }
  result.source = source; result.current = projected[0]; result.pending = projected[1];
  return result;
}

/* ---- nativeInventoryRows (5795-5834): rows, labels and icons are
 * thread-local storage valid until the next call. ---- */
typedef struct ss_inventory_rows {
  qa_compat_q2_native_inventory_row *rows; size_t count, capacity;
  qa_weapon_hud_icon *icons; size_t icon_count, icon_capacity;
  char (*paths)[160]; size_t path_count, path_capacity;
  qa_app_q3_source_inventory_row equipment[SS_Q3_INVENTORY];
  ss_ui_storage ui;
} ss_inventory_rows;
static __thread ss_inventory_rows ss_rows;
/* [...new Map(rows.map(row => [row.item, row])).values()]: a later row replaces the first one in place. */
static void ss_put_row(qa_compat_q2_native_inventory_row row) {
  for (size_t i = 0; i < ss_rows.count; i++) if (qa_ss_is(ss_rows.rows[i].item, row.item)) { ss_rows.rows[i] = row; return; }
  ss_rows.rows = ss_grow(ss_rows.rows, &ss_rows.capacity, ss_rows.count + 1, sizeof *ss_rows.rows);
  ss_rows.rows[ss_rows.count++] = row;
}
/* Icon storage is fixed per call so earlier row pointers stay valid. */
static qa_weapon_hud_icon *ss_row_icon(void) {
  if (ss_rows.icon_count == ss_rows.icon_capacity) ss_fail("Native inventory icons exceed their storage");
  qa_weapon_hud_icon *icon = &ss_rows.icons[ss_rows.icon_count++];
  memset(icon, 0, sizeof *icon);
  return icon;
}
static const char *ss_configstring(const qa_app_sim_configstring *values, size_t count, double index) {
  for (size_t i = 0; i < count; i++) if (values[i].index == index) return values[i].value;
  return NULL;
}
static qa_compat_q2_native_inventory_row ss_inventory_row(qa_item_id item, const char *label, double count, double index, bool selected) {
  qa_compat_q2_native_inventory_row row; memset(&row, 0, sizeof row);
  row.item = item; row.label = label; row.count = count; row.source_index = index; row.selected = selected;
  return row;
}
const qa_compat_q2_native_inventory_row *qa_app_shared_sim_native_inventory_rows(qa_app_shared_simulation *sim, qa_actor_id actor,
    size_t *out_count) {
  qa_compat_q2_native_primary_commands *commands = sim->native_primary_commands;
  bool selected = ss_sel_has(sim, actor);
  size_t definition_count = 0;
  const qa_source_item_definition *definitions = qa_shared_inventory_table_item_definitions(sim->inventory, actor, &definition_count);
  *out_count = 0;
  if (!ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE) || commands == NULL || (!selected && definition_count == 0)) return NULL;
  ss_rows.count = 0; ss_rows.icon_count = 0; ss_rows.path_count = 0;
  size_t slot_count = 0, label_count = 0;
  const qa_compat_q2_native_inventory_slot *slots = qa_compat_q2_native_primary_commands_inventory_slots(commands, &slot_count);
  const qa_app_sim_configstring *labels = ss_native_classic(sim)
      ? qa_app_sim_classic_guest_world_configstrings(sim->source.classic_world, &label_count)
      : qa_app_sim_rerelease_guest_world_configstrings(sim->source.rerelease_world, &label_count);
  double label_base = ss_native_classic(sim) ? 1056 : 11326;
  /* Every row may carry one icon; reserve before any row points into the storage. */
  size_t icon_need = slot_count + definition_count + SS_Q3_INVENTORY + 1;
  if (ss_rows.icon_capacity < icon_need) {
    free(ss_rows.icons); ss_rows.icons = calloc(icon_need, sizeof *ss_rows.icons);
    if (ss_rows.icons == NULL) qa_range_error("Out of memory");
    ss_rows.icon_capacity = icon_need;
  }
  if (ss_rows.path_capacity < slot_count + 1) {
    free(ss_rows.paths); ss_rows.paths = calloc(slot_count + 1, sizeof *ss_rows.paths);
    if (ss_rows.paths == NULL) qa_range_error("Out of memory");
    ss_rows.path_capacity = slot_count + 1;
  }
  /* native: slots that are no selected weapon or ammunition. */
  qa_compat_q2_native_inventory_row *native = calloc(slot_count + 1, sizeof *native);
  if (native == NULL) qa_range_error("Out of memory");
  size_t native_count = 0;
  for (size_t i = 0; i < slot_count; i++) {
    if (selected && (slots[i].weapon || slots[i].ammunition)) continue;
    const char *label = ss_configstring(labels, label_count, label_base + slots[i].index);
    native[native_count++] = ss_inventory_row(slots[i].item, label != NULL ? label : "", ss_count(sim, actor, slots[i].item), slots[i].index, false);
  }
  const qa_compat_q2_native_primary_inventory_profile *prototypes = ss_native_prototypes(sim);
  if (prototypes == NULL) { free(native); ss_fail("Native inventory has no declared original item prototypes"); }
  const qa_compat_q2_native_inventory_slot *weapon_prototype = NULL, *ammo_prototype = NULL, *usable_prototype = NULL, *passive_prototype = NULL;
  for (size_t i = 0; i < slot_count; i++) {
    if (weapon_prototype == NULL && qa_ss_is(slots[i].item, prototypes->proto_weapon)) weapon_prototype = &slots[i];
    if (ammo_prototype == NULL && qa_ss_is(slots[i].item, prototypes->proto_ammunition)) ammo_prototype = &slots[i];
    if (usable_prototype == NULL && qa_ss_is(slots[i].item, prototypes->proto_usable)) usable_prototype = &slots[i];
    if (passive_prototype == NULL && qa_ss_is(slots[i].item, prototypes->proto_passive)) passive_prototype = &slots[i];
  }
  if (weapon_prototype == NULL || ammo_prototype == NULL) { free(native); ss_fail("Original inventory lacks its declared weapon/ammo prototypes"); }
  /* weapons: the selected arsenal's weapon UI items. */
  if (selected) {
    qa_app_arsenal_ui ui = ss_sel_ui(sim, actor, sim->weapon_provider, &ss_rows.ui);
    for (size_t i = 0; i < ui.item_count; i++) {
      if (!qa_ss_is(ui.items[i].kind, "weapon")) continue;
      qa_compat_q2_native_inventory_row row = ss_inventory_row(ui.items[i].id, ui.items[i].label, ss_count(sim, actor, ui.items[i].id),
          weapon_prototype->index, true);
      row.has_presentation = true; row.presentation.source = sim->weapon_provider; row.presentation.kind = "weapon";
      row.presentation.weapon = ui.items[i].id;
      ss_put_row(row);
    }
  }
  size_t weapon_rows = ss_rows.count;
  /* ammunition: the catalog's distinct ammunition the weapon rows do not list. */
  if (selected) {
    size_t catalog_count = 0;
    const qa_selected_pickup_weapon *catalog = ss_sel_catalog(sim, &catalog_count);
    for (size_t i = 0; i < catalog_count; i++) {
      qa_item_id ammo = catalog[i].ammo;
      if (ammo == NULL) continue;
      bool seen = false;
      for (size_t j = 0; j < i && !seen; j++) seen = qa_ss_is(catalog[j].ammo, ammo);
      for (size_t j = 0; j < weapon_rows && !seen; j++) seen = qa_ss_is(ss_rows.rows[j].item, ammo);
      if (seen) continue;
      const qa_selected_pickup_weapon *weapon = NULL;
      for (size_t j = 0; j < catalog_count && weapon == NULL; j++) if (qa_ss_is(catalog[j].ammo, ammo)) weapon = &catalog[j];
      if (weapon == NULL) { free(native); ss_fail("Selected ammo has no authored weapon presentation"); }
      qa_compat_q2_native_inventory_row row = ss_inventory_row(ammo, qa_app_selected_ammo_label(sim->selected_arsenal.family, ammo),
          ss_count(sim, actor, ammo), ammo_prototype->index, true);
      row.has_presentation = true; row.presentation.source = sim->weapon_provider; row.presentation.kind = "ammunition";
      row.presentation.weapon = weapon->item;
      ss_put_row(row);
    }
  }
  size_t ammunition_rows = ss_rows.count;
  /* retainedAmmo: original ammunition the selected supply still maps onto. */
  if (selected) {
    for (size_t i = 0; i < slot_count; i++) {
      if (!slots[i].ammunition) continue;
      bool listed = false;
      for (size_t j = weapon_rows; j < ammunition_rows && !listed; j++) listed = qa_ss_is(ss_rows.rows[j].item, slots[i].item);
      if (listed) continue;
      const qa_pickup_supply_profile *supply = sim->selected_original_supply;
      bool mapped = false;
      for (size_t j = 0; supply != NULL && j < supply->ammo_count && !mapped; j++)
        for (size_t k = 0; k < supply->ammo[j].destination_count && !mapped; k++) mapped = qa_ss_is(supply->ammo[j].destinations[k], slots[i].item);
      if (!mapped) continue;
      const char *label = ss_configstring(labels, label_count, label_base + slots[i].index);
      qa_compat_q2_native_inventory_row row = ss_inventory_row(slots[i].item, label != NULL ? label : "", ss_count(sim, actor, slots[i].item),
          ammo_prototype->index, true);
      row.has_presentation = true; row.presentation.source = sim->recipe.map.entities; row.presentation.kind = "item";
      row.presentation.has_icon = true;
      if (slots[i].icon != NULL && slots[i].icon[0] != '\0') {
        qa_weapon_hud_icon *icon = ss_row_icon();
        char *path = ss_rows.paths[ss_rows.path_count++];
        if (slots[i].icon[0] == '/' || slots[i].icon[0] == '\\') snprintf(path, sizeof ss_rows.paths[0], "%s", slots[i].icon + 1);
        else snprintf(path, sizeof ss_rows.paths[0], "pics/%s.pcx", slots[i].icon);
        icon->kind = QA_WEAPON_HUD_ICON_IMAGE; icon->resource.content = sim->recipe.map.entities.content; icon->resource.path = path;
        row.presentation.icon = icon;
      }
      ss_put_row(row);
    }
  }
  for (size_t i = 0; i < native_count; i++) ss_put_row(native[i]);
  free(native);
  if (usable_prototype == NULL || passive_prototype == NULL) ss_fail("Original inventory lacks usable and passive item prototypes");
  qa_owned_actor owner;
  if (ss_owned(sim, actor, &owner) && sim->selected_q3_source != NULL) {
    size_t count = qa_app_q3_selected_source_inventory(sim->selected_q3_source, owner, ss_rows.equipment, SS_Q3_INVENTORY);
    if (count > SS_Q3_INVENTORY) ss_fail("Selected Q3 inventory exceeds its projection storage");
    for (size_t i = 0; i < count; i++) {
      const qa_app_q3_source_inventory_row *item = &ss_rows.equipment[i];
      qa_compat_q2_native_inventory_row row = ss_inventory_row(item->item, item->label, item->count,
          item->usable ? usable_prototype->index : passive_prototype->index, true);
      row.has_presentation = true; row.presentation.source = sim->weapon_provider; row.presentation.kind = "item";
      row.presentation.has_icon = true;
      if (item->icon != NULL) {
        qa_weapon_hud_icon *icon = ss_row_icon();
        icon->kind = QA_WEAPON_HUD_ICON_SHADER; icon->content = sim->weapon_provider.content; icon->name = item->icon;
        row.presentation.icon = icon;
      }
      ss_put_row(row);
    }
  }
  for (size_t i = 0; i < definition_count; i++) {
    const qa_source_item_definition *item = &definitions[i];
    double index;
    if (item->is_weapon) index = weapon_prototype->index;
    else if (ss_definition_has_action(item, QA_SOURCE_ITEM_ACTION_USE)) index = usable_prototype->index;
    else {
      bool ammunition = false;
      for (size_t j = 0; j < definition_count && !ammunition; j++)
        ammunition = definitions[j].is_weapon && qa_ss_is(definitions[j].weapon.ammo, item->item);
      index = ammunition ? ammo_prototype->index : passive_prototype->index;
    }
    qa_compat_q2_native_inventory_row row = ss_inventory_row(item->item, item->label, ss_count(sim, actor, item->item), index, true);
    row.presence_only = true;
    row.has_presentation = true; row.presentation.source = item->source; row.presentation.kind = "item";
    row.presentation.has_icon = true; row.presentation.icon = item->has_icon ? item->icon : NULL;
    ss_put_row(row);
  }
  *out_count = ss_rows.count;
  return ss_rows.rows;
}

/* ---- primaryPresentations (7571-7700): thread-local storage valid until
 * the next call; strings it builds are interned in the simulation. ---- */
typedef struct ss_presentation_list { qa_app_simulation_presentation *items; size_t count, capacity; } ss_presentation_list;
static qa_app_simulation_presentation *ss_push_presentation(ss_presentation_list *list, const qa_app_simulation_presentation *value) {
  list->items = ss_grow(list->items, &list->capacity, list->count + 1, sizeof *list->items);
  list->items[list->count] = *value;
  return &list->items[list->count++];
}
/* A producer that reports its TS count and writes at most capacity rows. */
static void ss_push_produced(ss_presentation_list *list, size_t (*produce)(void *ctx, qa_app_simulation_presentation *out, size_t capacity),
    void *ctx) {
  size_t capacity = 64;
  for (;;) {
    list->items = ss_grow(list->items, &list->capacity, list->count + capacity, sizeof *list->items);
    size_t produced = produce(ctx, list->items + list->count, capacity);
    if (produced <= capacity) { list->count += produced; return; }
    capacity = produced;
  }
}
static size_t ss_q3_runtime_presentations(void *ctx, qa_app_simulation_presentation *out, size_t capacity) {
  return qa_app_q3_source_runtime_presentations(ctx, out, capacity);
}
static size_t ss_selected_q3_presentations(void *ctx, qa_app_simulation_presentation *out, size_t capacity) {
  return qa_app_q3_selected_source_presentations(ctx, out, capacity);
}
static const char *ss_native_configstring(qa_app_shared_simulation *sim, double index) {
  size_t count = 0;
  const qa_app_sim_configstring *values = ss_native_classic(sim)
      ? qa_app_sim_classic_guest_world_configstrings(sim->source.classic_world, &count)
      : qa_app_sim_rerelease_guest_world_configstrings(sim->source.rerelease_world, &count);
  return ss_configstring(values, count, index);
}
/* The q2-native entity models (7575-7608). */
static void ss_native_entity_presentations(qa_app_shared_simulation *sim, ss_presentation_list *list, double fraction, double back_lerp) {
  bool classic = ss_native_classic(sim);
  qa_content_id content = sim->recipe.map.entities.content;
  size_t count = 0;
  const qa_q2_entity_state *classic_states = classic ? qa_app_sim_classic_guest_world_entity_states(sim->source.classic_world, &count) : NULL;
  const qa_q2_rerelease_entity_state *rerelease_states = classic ? NULL
      : qa_app_sim_rerelease_guest_world_entity_states(sim->source.rerelease_world, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_q2_entity_state *state = classic ? &classic_states[i] : &rerelease_states[i].base;
    bool has_actor, active; qa_actor_id actor; double server_flags;
    if (classic) {
      qa_app_sim_classic_guest_entity_info info = qa_app_sim_classic_guest_world_entity_info(sim->source.classic_world, state->number);
      has_actor = info.has_actor; actor = info.actor; active = info.active; server_flags = info.server_flags;
    } else {
      qa_app_sim_rerelease_entity_info info = qa_app_sim_rerelease_guest_world_entity_info(sim->source.rerelease_world, state->number);
      has_actor = info.has_actor; actor = info.actor; active = info.active; server_flags = info.server_flags;
    }
    if (state->number == 0 || !has_actor || !active || (qa_js_to_int32(server_flags) & 1) != 0) continue;
    /* A foreign selected character replaces the guest player model; the
     * selected-character presentation below draws this actor. */
    if (qa_app_shared_sim_foreign_native_character(sim) && qa_app_shared_sim_native_character_host(sim, actor) != NULL) continue;
    qa_app_sim_model_appearance appearance = classic ? qa_app_sim_classic_guest_world_model_appearance(sim->source.classic_world, state->number)
        : qa_app_sim_rerelease_guest_world_model_appearance(sim->source.rerelease_world, state->number);
    qa_vec3 delta = qa_ss_subtract(state->origin, state->old_origin);
    bool continuous = fraction < 1 && delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <= 512 * 512;
    qa_app_simulation_presentation model; memset(&model, 0, sizeof model);
    model.actor = actor; model.content = content; model.family = QA_GAME_FAMILY_Q2;
    model.frame = state->frame; model.old_frame = classic ? state->frame : rerelease_states[i].old_frame;
    if (continuous) { model.has_back_lerp = true; model.back_lerp = back_lerp; }
    model.skin = appearance.skin; model.has_skin_path = true; model.skin_path = appearance.skin_path;
    model.effects = classic ? state->effects : (double)rerelease_states[i].effects; model.render_flags = state->render_effects;
    model.origin = continuous ? (qa_vec3){state->old_origin.x + delta.x * fraction, state->old_origin.y + delta.y * fraction,
        state->old_origin.z + delta.z * fraction} : state->origin;
    model.has_previous_origin = true; model.previous_origin = state->old_origin; model.angles = state->angles;
    model.scale = !classic && rerelease_states[i].scale != 0 ? rerelease_states[i].scale : 1;
    model.has_alpha = true;
    model.alpha = classic ? 1 : rerelease_states[i].alpha != 0 ? rerelease_states[i].alpha : (qa_js_to_int32(state->render_effects) & 32) != 0 ? 0.3 : 1;
    model.visible = true; model.view_weapon = false;
    /* A body-queue corpse copies the guest player model (modelindex 255). With a Quake 1 character selected it is
     * drawn as that character's last death frame; Quake III characters keep the guest corpse model. */
    if (appearance.path != NULL && strncmp(appearance.path, "players/", 8) == 0 && qa_app_shared_sim_foreign_native_character(sim)
        && qa_app_sim_provider_family(sim->recipe.character.definition.provider) == QA_GAME_FAMILY_Q1) {
      qa_app_simulation_presentation corpse = model;
      corpse.content = sim->recipe.character.definition.content; corpse.family = QA_GAME_FAMILY_Q1; corpse.path = "progs/player.mdl";
      corpse.frame = 60; corpse.old_frame = 60; /* Q1_CORPSE_FRAME */
      corpse.skin = 0; corpse.has_skin_path = true; corpse.skin_path = NULL; corpse.effects = 0; corpse.render_flags = 0;
      corpse.has_player_colors = true;
      corpse.player_color_top = sim->options.has_player_colors ? sim->options.player_color_top : 0;
      corpse.player_color_bottom = sim->options.has_player_colors ? sim->options.player_color_bottom : 0;
      ss_push_presentation(list, &corpse);
      continue;
    }
    if (appearance.path != NULL && appearance.path[0] != '\0') { model.path = appearance.path; ss_push_presentation(list, &model); }
    for (size_t index = 0; index < appearance.attached_model_count; index++) {
      const char *path = appearance.attached_models[index];
      if (path == NULL || path[0] == '\0') continue;
      qa_app_simulation_presentation attached = model;
      attached.path = path; attached.skin = 0; attached.has_skin_path = true; attached.skin_path = NULL;
      if (index == 0 && ss_sel_has(sim, actor)) attached.native_held_weapon = true;
      ss_push_presentation(list, &attached);
    }
  }
  for (size_t i = 0; i < sim->source.clients.count; i++) {
    qa_client_id client = qa_ss_map_key_at(&sim->source.clients, i)->client;
    qa_owned_actor actor = *(const qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i);
    if (ss_sel_has(sim, actor.id)) continue;
    double slot = (double)client.slot + 1;
    qa_q2_player_view state; double gun_skin = 0;
    if (classic) state = qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot).view;
    else {
      qa_q2_rerelease_player_state rerelease = qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot);
      state = rerelease.view; gun_skin = rerelease.gun_skin;
    }
    qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor.id);
    const char *path = ss_native_configstring(sim, (classic ? 32 : 62) + state.gun_index);
    if (state.gun_index == 0 || path == NULL || path[0] == '\0') continue;
    qa_vec3 eye = qa_ss_add(view.origin, (qa_vec3){0, 0, view.view_height});
    qa_app_simulation_presentation model; memset(&model, 0, sizeof model);
    model.actor = actor.id; model.content = content; model.family = QA_GAME_FAMILY_Q2; model.path = path;
    model.frame = state.gun_frame; model.old_frame = state.gun_frame; model.skin = gun_skin; model.effects = 0; model.render_flags = 0;
    model.origin = qa_ss_add(eye, state.gun_offset); model.has_view_origin = true; model.view_origin = eye;
    model.has_view_angles = true; model.view_angles = view.angles; model.angles = qa_ss_add(view.angles, state.gun_angles);
    model.scale = 1; model.visible = true; model.view_weapon = true;
    ss_push_presentation(list, &model);
  }
}
/* The shared actor executions (7612-7647). */
static void ss_execution_presentations(qa_app_shared_simulation *sim, ss_presentation_list *list, double fraction, qa_vec3 *rims) {
  for (size_t i = 0; i < sim->actor_executions.count; i++) {
    const qa_app_sim_actor_execution *entry = qa_ss_map_value_at(&sim->actor_executions, i);
    if (entry->kind != QA_APP_SIM_ACTOR_EXECUTION_Q1 && entry->kind != QA_APP_SIM_ACTOR_EXECUTION_Q2) continue;
    bool q1 = entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1;
    qa_actor_id id = q1 ? entry->q1_entity->actor.id : entry->q2_entity->actor.id;
    qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, id, &body);
    if (sim->selected_monsters != NULL && !qa_app_sim_selected_monsters_active(sim->selected_monsters, id)) continue;
    const char *entity_model = q1 ? entry->q1_entity->model : entry->q2_entity->model;
    qa_scene_flare flare;
    bool has_flare = !q1 && qa_q2_entity_flare(entry->q2_entity, &flare, &rims[i]);
    bool empty_model = entity_model == NULL || entity_model[0] == '\0';
    if (!has_body || qa_app_shared_sim_player(sim, id) != NULL || (empty_model && !has_flare)) continue;
    qa_app_simulation_presentation model; memset(&model, 0, sizeof model);
    if (q1) {
      const qa_q1_actor *entity = entry->q1_entity;
      double alpha = qa_q1_actor_number(entity, "alpha"), scale = qa_q1_actor_number(entity, "scale");
      if (qa_ss_is(entity->model, sim->recipe.map.geometry.requested_path)) continue;
      qa_ss_interpolated_pose pose = qa_app_shared_sim_interpolated_pose(sim, id, body.origin, body.angles, fraction);
      qa_ss_visual_fields visual = qa_app_shared_sim_q1_visual_fields(alpha, scale);
      model.actor = id; model.content = entry->content; model.family = QA_GAME_FAMILY_Q1; model.path = entity->model;
      model.frame = entity->frame; model.old_frame = entity->frame; model.skin = entity->skin; model.effects = entity->effects;
      model.render_flags = 0; model.origin = pose.origin; model.has_previous_origin = true; model.previous_origin = pose.previous_origin;
      model.has_back_lerp = true; model.back_lerp = pose.back_lerp; model.angles = pose.angles;
      model.has_alpha = true; model.alpha = visual.alpha; model.scale = visual.scale; model.visible = true; model.view_weapon = false;
      ss_push_presentation(list, &model);
      continue;
    }
    const qa_q2_entity *entity = entry->q2_entity;
    const qa_q2_presentation_event *source = qa_ss_map_get(&sim->source_models, &id);
    if (qa_ss_is(entity->classname, "worldspawn")) continue;
    double frame = source != NULL ? source->frame : entity->frame, old_frame = source != NULL ? source->old_frame : entity->old_frame;
    bool rerelease = qa_q2_entity_services_game(entry->q2_services)->options.edition == QA_Q2_EDITION_RERELEASE;
    bool beam = rerelease && (qa_js_to_int32(entity->render_flags) & 128) != 0;
    qa_ss_interpolated_pose pose;
    if (beam) { pose.origin = body.origin; pose.angles = body.angles; pose.previous_origin = entity->pos2; pose.back_lerp = 0; }
    else pose = qa_app_shared_sim_interpolated_pose(sim, id, body.origin, body.angles, fraction);
    model.actor = id; model.content = entry->content; model.family = QA_GAME_FAMILY_Q2;
    model.frame = frame; model.old_frame = old_frame == -1 ? frame : old_frame; model.has_back_lerp = true; model.back_lerp = pose.back_lerp;
    model.skin = source != NULL ? source->skin : entity->skin; model.effects = source != NULL ? source->effects : entity->effects;
    model.render_flags = source != NULL ? source->render_flags : entity->render_flags;
    model.has_alpha = true; model.alpha = source != NULL ? source->alpha : entity->alpha;
    model.origin = pose.origin; model.angles = pose.angles; model.has_previous_origin = true; model.previous_origin = pose.previous_origin;
    model.scale = source != NULL ? source->scale : 1;
    model.visible = entity->visible && (qa_js_to_int32(entity->server_flags) & 1) == 0; model.view_weapon = false;
    if (!empty_model || has_flare) {
      qa_app_simulation_presentation primary = model;
      primary.path = entity_model != NULL ? entity_model : "";
      if (has_flare) { primary.has_flare = true; primary.flare = flare; } /* rim_color points into the call's rim storage */
      if (rerelease && (qa_js_to_int32(entity->render_flags) & 128) != 0) { primary.has_model_beam = true; primary.model_beam_segment_length = entity->frame; }
      ss_push_presentation(list, &primary);
    }
    /* Q2 modelindex2-4 draw with the same frame/skin as the primary model (e.g. the Jorg shell over its rider). */
    const char *attached[3] = {entity->model2, entity->model3, entity->model4};
    if (source != NULL) for (int k = 0; k < 3; k++) attached[k] = source->attached_models[k];
    for (int k = 0; k < 3; k++) {
      if (attached[k] == NULL || attached[k][0] == '\0') continue;
      qa_app_simulation_presentation extra = model; extra.path = attached[k];
      ss_push_presentation(list, &extra);
    }
  }
}
/* The movement players' character bodies (7648-7678). */
static void ss_player_presentations(qa_app_shared_simulation *sim, ss_presentation_list *list, double fraction) {
  size_t player_count = 0;
  qa_app_sim_movement_player *const *players = qa_app_shared_sim_presentation_players(sim, &player_count);
  for (size_t i = 0; i < player_count; i++) {
    qa_app_sim_movement_player *player = players[i];
    if (player->character == QA_GAME_FAMILY_Q3 || player->intermission || player->has_cutscene) continue;
    qa_body_state body;
    if (!qa_app_shared_sim_body(sim, player->actor.id, &body)) continue;
    const char *provider = player->character_appearance.provider, *slash = strrchr(provider, '/');
    const char *model_name = slash != NULL ? slash + 1 : provider;
    qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &player->actor);
    const qa_q2_entity *q2_entity = character != NULL ? &(*character)->entity
        : player->character == QA_GAME_FAMILY_Q2 ? sim->strategy->q2_player_entity(sim, player->actor.id) : NULL;
    bool q2_gibbed = character != NULL ? (*character)->state.gibbed
        : player->character == QA_GAME_FAMILY_Q2 && sim->strategy->q2_gibbed(sim, player->actor.id);
    qa_q1_player_state *q1_player = sim->strategy->q1_player(sim, player->actor.id);
    qa_comp_q1_source_client *source_colors = player->character == QA_GAME_FAMILY_Q1 ? sim->strategy->q1_colors(sim, player->actor.id) : NULL;
    /* Foreign Q1 characters have no Q1 client record: fall back to the
     * launch appearance (bots wear the default translation) so the skin
     * is never rendered with untranslated palette ramps. */
    bool has_colors = player->character == QA_GAME_FAMILY_Q1; double shirt = 0, pants = 0;
    if (has_colors && source_colors != NULL) {
      shirt = (double)qa_comp_q1_source_client_shirt(source_colors); pants = (double)qa_comp_q1_source_client_pants(source_colors);
    } else if (has_colors && !qa_app_sim_bot_services_is_bot(&sim->bot_services, player->actor.id) && sim->options.has_player_colors) {
      shirt = sim->options.player_color_top; pants = sim->options.player_color_bottom;
    }
    qa_ss_visual_fields visual = q1_player == NULL ? (qa_ss_visual_fields){1, 1} : qa_app_shared_sim_q1_visual_fields(q1_player->alpha, q1_player->scale);
    qa_ss_interpolated_pose pose = qa_app_shared_sim_interpolated_pose(sim, player->actor.id, body.origin, body.angles, fraction);
    const char *path = q2_entity != NULL && q2_entity->model != NULL && q2_entity->model[0] != '\0' ? q2_entity->model : NULL;
    if (path == NULL) {
      qa_q1_character_actor **q1_character = qa_ss_map_get(&sim->q1_characters, &player->actor);
      if (q1_character != NULL) {
        qa_q1_character_presentation presentation = qa_q1_character_actor_presentation(*q1_character);
        if (presentation.model[0] != '\0') path = qa_app_shared_sim_intern(sim, presentation.model);
      }
    }
    char text[192];
    if (path == NULL) {
      if (player->character == QA_GAME_FAMILY_Q1) path = "progs/player.mdl";
      else { snprintf(text, sizeof text, "players/%s/tris.md2", model_name); path = qa_app_shared_sim_intern(sim, text); }
    }
    qa_app_simulation_presentation model; memset(&model, 0, sizeof model);
    model.actor = player->actor.id; model.content = player->character_appearance.content; model.family = player->character;
    model.path = path; model.has_skin_path = true;
    if (player->character == QA_GAME_FAMILY_Q2 && !q2_gibbed) {
      snprintf(text, sizeof text, "players/%s/grunt.pcx", model_name); model.skin_path = qa_app_shared_sim_intern(sim, text);
    }
    qa_animation_state animation = player->animation.state;
    model.frame = q2_entity != NULL ? q2_entity->frame : qa_ss_is(animation.kind, "q1") || qa_ss_is(animation.kind, "q2") ? animation.frame : 0;
    model.old_frame = q2_entity != NULL ? q2_entity->old_frame : 0;
    model.has_back_lerp = true; model.back_lerp = pose.back_lerp;
    model.skin = q2_entity != NULL ? q2_entity->skin : 0; model.effects = q2_entity != NULL ? q2_entity->effects : 0;
    model.render_flags = q2_entity != NULL ? q2_entity->render_flags : 0;
    model.origin = pose.origin; model.has_previous_origin = true; model.previous_origin = pose.previous_origin; model.angles = pose.angles;
    model.has_alpha = true; model.alpha = visual.alpha; model.scale = visual.scale;
    if (has_colors) { model.has_player_colors = true; model.player_color_top = shirt; model.player_color_bottom = pants; }
    model.visible = true; model.view_weapon = false;
    ss_push_presentation(list, &model);
  }
}
/* The first-person weapon models (7684-7712). */
static void ss_weapon_model_presentation(qa_app_shared_simulation *sim, ss_presentation_list *list, qa_actor_id actor, const char *path,
    double frame, double fraction, double back_lerp) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  qa_client_id client;
  if (player == NULL ? !sim->strategy->guest_weapon_views(sim) || !qa_app_shared_sim_player_client(sim, actor, &client)
                     : player->intermission || player->has_cutscene) return;
  qa_game_family weapon_family = qa_app_sim_provider_family(sim->weapon_provider.provider);
  const qa_ss_view_model *view_model = qa_ss_map_get(&sim->view_models, &actor);
  bool has_q2 = weapon_family == QA_GAME_FAMILY_Q2 && view_model != NULL && view_model->has_q2;
  const qa_q2_base_player_view *native_view = sim->strategy->uses_q2_views(sim) && player != NULL && player->character == QA_GAME_FAMILY_Q2
      ? qa_ss_map_get(&sim->q2_views, &actor) : NULL;
  qa_app_sim_player_view view = native_view == NULL && player != NULL ? qa_app_sim_movement_player_view(player) : qa_app_shared_sim_player_view(sim, actor);
  const qa_q2_base_player_view *source_view = has_q2 ? native_view : NULL;
  bool has_arsenal = sim->has_selected_arsenal;
  qa_arsenal_state arsenal; memset(&arsenal, 0, sizeof arsenal);
  if (has_arsenal) arsenal = ss_sel_read(sim, actor);
  qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor, &body);
  qa_app_simulation_presentation model; memset(&model, 0, sizeof model);
  model.actor = actor;
  if (has_arsenal && ss_family_is(sim, "q3") && qa_ss_is(arsenal.state.kind, "q3") && has_body) {
    double torso = 0, last_fire = 0; bool has_last_fire = false;
    qa_app_q3_selected_arsenal_view_state(sim->selected_arsenal.q3, actor, &torso, &has_last_fire, &last_fire);
    double buttons;
    if (player != NULL) buttons = player->buttons;
    else {
      const qa_mod_client_command_receipt *receipt = qa_ss_map_get(&sim->mod_client_commands, &actor);
      buttons = receipt != NULL ? ss_buttons(&receipt->input.command) : 0;
    }
    model.has_q3_weapon = true; model.q3_weapon_torso_animation = torso;
    model.has_q3_weapon_last_fire = has_last_fire; model.q3_weapon_last_fire_ms = last_fire;
    model.q3_weapon_time_ms = trunc(sim->selected_milliseconds); model.q3_weapon_weapon = arsenal.state.source_weapon;
    model.q3_weapon_firing = (qa_js_to_int32(buttons) & 1) != 0 && ss_health_or(sim, actor, 0) > 0;
    model.q3_weapon_horizontal_speed = qa_js_hypot2(body.velocity.x, body.velocity.y);
    if (player != NULL && player->state.kind == QA_MOVEMENT_STATE_Q3) model.q3_weapon_bob_cycle = player->state.as.q3.bob_cycle;
    else model.q3_weapon_bob_cycle = sim->strategy->guest_bob_cycle(sim, actor);
  }
  if (has_arsenal && arsenal.active_weapon != NULL) model.weapon_item = arsenal.active_weapon;
  if (has_arsenal) ss_arsenal_free(&arsenal);
  const qa_ss_presentation_pose *snapshot = qa_ss_map_get(&sim->presentation_snapshot, &actor);
  bool interpolated = snapshot != NULL && snapshot->view_path != NULL && qa_ss_is(snapshot->view_path, path) && fraction < 1;
  qa_vec3 eye = qa_ss_add(view.origin, (qa_vec3){0, 0, view.view_height});
  model.content = sim->weapon_provider.content; model.family = weapon_family; model.path = path; model.frame = frame;
  model.old_frame = interpolated ? snapshot->view_frame : frame; model.has_back_lerp = true; model.back_lerp = interpolated ? back_lerp : 0;
  model.skin = has_q2 ? view_model->q2_skin : 0; model.effects = 0; model.render_flags = weapon_family == QA_GAME_FAMILY_Q2 ? 1 | 4 | 16 : 0;
  qa_vec3 offset = source_view != NULL ? source_view->gun_offset : has_q2 ? view_model->q2_kick_origin : qa_ss_zero;
  qa_vec3 turn = source_view != NULL ? source_view->gun_angles : has_q2 ? view_model->q2_kick_angles : qa_ss_zero;
  model.origin = qa_ss_add(eye, offset); model.has_view_origin = true; model.view_origin = eye;
  model.has_view_angles = true; model.view_angles = view.angles; model.angles = qa_ss_add(view.angles, turn); model.scale = 1;
  bool visible;
  model.visible = sim->strategy->weapon_visible(sim, actor, &visible) ? visible : ss_health_or(sim, actor, 0) > 0;
  model.view_weapon = true;
  ss_push_presentation(list, &model);
}
/* Q2NativeStrategy.weaponVisible / Q3QvmStrategy.guestBobCycle */
bool qa_ss_q2_native_weapon_visible(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  double gun = ss_native_classic(sim) ? qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot).view.gun_index
      : qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot).view.gun_index;
  *out = gun != 0 && qa_compat_q2_native_primary_weapons_available(qa_app_shared_sim_require_native_weapons(sim), actor);
  return true;
}
double qa_ss_q3_qvm_guest_bob_cycle(qa_app_shared_simulation *sim, qa_actor_id actor) {
  ss_qvm_state state; ss_qvm_actor_state(sim, actor, &state);
  return state.state.bob_cycle;
}
/* SourceStrategy.presentations (Q2Native, Q3): appended to the presentation list (an ss_presentation_list). */
void qa_ss_q2_native_presentations(qa_app_shared_simulation *sim, double fraction, double back_lerp, void *list) {
  ss_native_entity_presentations(sim, list, fraction, back_lerp);
}
void qa_ss_q3_presentations(qa_app_shared_simulation *sim, double fraction, double back_lerp, void *list) {
  (void)fraction; (void)back_lerp; /* TS Q3Strategy.presentations() */
  ss_push_produced(list, ss_q3_runtime_presentations, sim->source.q3);
}
const qa_app_simulation_presentation *qa_app_shared_sim_primary_presentations(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread ss_presentation_list list;
  list.count = 0;
  qa_ss_presentation_fraction timing = qa_app_shared_sim_presentation_fraction(sim);
  sim->strategy->presentations(sim, timing.fraction, timing.back_lerp, &list);
  if (sim->selected_q3_source != NULL) ss_push_produced(&list, ss_selected_q3_presentations, sim->selected_q3_source);
  static __thread qa_vec3 *rims; static __thread size_t rim_capacity;
  rims = ss_grow(rims, &rim_capacity, sim->actor_executions.count + 1, sizeof *rims);
  ss_execution_presentations(sim, &list, timing.fraction, rims);
  ss_player_presentations(sim, &list, timing.fraction);
  for (size_t i = 0; i < sim->detached_models.count; i++) {
    qa_owned_actor actor = qa_ss_map_key_at(&sim->detached_models, i)->owned;
    const qa_ss_detached_model *detached = qa_ss_map_value_at(&sim->detached_models, i);
    qa_body_state body;
    if (!qa_app_shared_sim_body(sim, actor.id, &body)) continue;
    qa_app_simulation_presentation model; memset(&model, 0, sizeof model);
    model.actor = actor.id; model.content = detached->content;
    model.family = detached->family != NULL && qa_ss_is(detached->family, "q1") ? QA_GAME_FAMILY_Q1 : QA_GAME_FAMILY_Q2;
    model.path = detached->path; model.frame = 0; model.old_frame = 0; model.skin = detached->has_skin ? detached->skin : 0;
    model.effects = detached->has_effects ? detached->effects : 2; model.render_flags = 0; model.origin = body.origin; model.angles = body.angles;
    model.scale = 1; model.visible = true; model.view_weapon = false;
    ss_push_presentation(&list, &model);
  }
  if (!sim->has_selected_arsenal) {
    for (size_t i = 0; i < sim->view_models.count; i++) {
      qa_actor_id actor = qa_ss_map_key_at(&sim->view_models, i)->actor;
      const qa_ss_view_model *model = qa_ss_map_value_at(&sim->view_models, i);
      ss_weapon_model_presentation(sim, &list, actor, model->path, model->frame, timing.fraction, timing.back_lerp);
    }
  } else {
    /* A connected bot can sit in the Q3 spawn queue before ClientBegin
     * admits its selected arsenal; it has no first-person weapon yet. */
    size_t actor_count = 0;
    const qa_actor_id *actors = qa_app_shared_sim_players(sim, &actor_count);
    qa_actor_id *ordered = malloc((actor_count ? actor_count : 1) * sizeof *ordered);
    qa_app_arsenal_view *views = malloc((actor_count ? actor_count : 1) * sizeof *views);
    if (ordered == NULL || views == NULL) qa_range_error("Out of memory");
    size_t view_count = 0;
    for (size_t i = 0; i < actor_count; i++) {
      if (!ss_sel_has(sim, actors[i])) continue;
      qa_app_arsenal_view view = sim->selected_arsenal.q1 ? qa_app_q1_selected_arsenal_view(sim->selected_arsenal.q1, actors[i])
          : sim->selected_arsenal.q2 ? qa_app_q2_selected_arsenal_view(sim->selected_arsenal.q2, actors[i])
          : qa_app_q3_selected_arsenal_view(sim->selected_arsenal.q3, actors[i]);
      if (!view.has_value) continue;
      /* new Map(...): a repeated actor keeps its first position with the last view. */
      size_t at = view_count;
      for (size_t j = 0; j < view_count; j++) if (qa_same_actor(ordered[j], actors[i])) { at = j; break; }
      ordered[at] = actors[i]; views[at] = view;
      if (at == view_count) view_count++;
    }
    for (size_t i = 0; i < view_count; i++)
      ss_weapon_model_presentation(sim, &list, ordered[i], views[i].path, views[i].frame, timing.fraction, timing.back_lerp);
    free(ordered); free(views);
  }
  *out_count = list.count;
  return list.items;
}

/* ---- presentations (7556-7569): thread-local storage valid until the next call. ---- */
static bool ss_overridden(const qa_session_mod_appearance_group *groups, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++) if (qa_same_actor(groups[i].actor, actor)) return true;
  return false;
}
const qa_app_simulation_presentation *qa_app_shared_sim_presentations(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread ss_presentation_list models, result;
  models.count = 0; result.count = 0;
  size_t group_count = 0, count = 0;
  const qa_session_mod_appearance_group *groups = sim->mod_owner != NULL ? qa_session_mods_appearance_overrides(sim->mod_owner, &group_count) : NULL;
  const qa_app_simulation_presentation *primary = qa_app_shared_sim_primary_presentations(sim, &count);
  for (size_t i = 0; i < count; i++) ss_push_presentation(&models, &primary[i]);
  if (sim->mod_owner != NULL) {
    const qa_app_simulation_presentation *mods = qa_session_mods_presentations(sim->mod_owner, &count);
    for (size_t i = 0; i < count; i++) ss_push_presentation(&models, &mods[i]);
  }
  if (sim->grapple != NULL && qa_ss_is(qa_app_sim_grapple_runtime_source(sim->grapple)->kind, "q3-qvm")) {
    const qa_app_simulation_presentation *hooks = qa_app_sim_qvm_grapple_source_presentations(qa_app_sim_grapple_runtime_source(sim->grapple)->q3_game,
        qa_app_sim_grapple_runtime_selection(sim->grapple)->source.content, &count);
    for (size_t i = 0; i < count; i++) ss_push_presentation(&models, &hooks[i]);
  }
  if (groups != NULL && group_count != 0) {
    size_t kept = 0;
    for (size_t i = 0; i < models.count; i++)
      if (models.items[i].view_weapon || !ss_overridden(groups, group_count, models.items[i].actor)) models.items[kept++] = models.items[i];
    models.count = kept;
    for (size_t i = 0; i < group_count; i++)
      for (size_t j = 0; j < groups[i].count; j++) {
        qa_app_simulation_presentation model = groups[i].presentations[j]; model.replaces_body = true;
        ss_push_presentation(&models, &model);
      }
  }
  if (sim->weapon_slots.count == 0) { *out_count = models.count; return models.items; }
  for (size_t i = 0; i < models.count; i++)
    if (!models.items[i].view_weapon || !qa_ss_map_has(&sim->weapon_slots, &models.items[i].actor)) ss_push_presentation(&result, &models.items[i]);
  for (size_t i = 0; i < sim->weapon_slots.count; i++) {
    qa_actor_id actor = qa_ss_map_key_at(&sim->weapon_slots, i)->actor;
    const qa_app_simulation_presentation *found = NULL;
    for (size_t j = 0; j < models.count && found == NULL; j++)
      if (models.items[j].view_weapon && qa_same_actor(models.items[j].actor, actor)) found = &models.items[j];
    qa_app_sim_weapon_slot_projection projection = qa_app_shared_sim_slot_projection(sim, actor, found);
    if (projection.model != NULL) ss_push_presentation(&result, projection.model);
  }
  *out_count = result.count;
  return result.items;
}

/* ---- C-only owners of the B2 constructions, released by qa_app_shared_sim_arsenal_destroy ---- */
static void ss_own(qa_app_shared_simulation *sim, void (*dispose)(void *ctx), void *ctx) {
  sim->b2_owned = ss_grow(sim->b2_owned, &sim->b2_owned_capacity, sim->b2_owned_count + 1, sizeof *sim->b2_owned);
  sim->b2_owned[sim->b2_owned_count++] = (qa_ss_dispose){ctx, dispose};
}
static void ss_dispose_random(void *ctx) { qa_app_sim_source_random_destroy(ctx); }
static void ss_dispose_q2_game(void *ctx) { qa_q2_entity_services_destroy(ctx); }
static void ss_dispose_q1_game(void *ctx) { qa_q1_services_free(ctx); }
static void ss_dispose_ballistics(void *ctx) { qa_q2w_ballistics_destroy(ctx); }
static void ss_dispose_hand_grenades(void *ctx) { qa_q2_hand_grenade_equipment_destroy(ctx); }
static void ss_dispose_threewave(void *ctx) { qa_q1_threewave_grapple_free(ctx); }
static void ss_dispose_ctf_grapple(void *ctx) { qa_q2_ctf_grapple_destroy(ctx); }
static void ss_dispose_lmctf_grapple(void *ctx) { qa_lmctf_grapple_destroy(ctx); }
static void ss_dispose_owned_profile(void *ctx) { qa_composition_owned_profile_destroy(ctx); }
static void ss_dispose_free(void *ctx) { free(ctx); }
static void ss_dispose_localization(void *ctx) { qa_localization_table_destroy(ctx); }
static void ss_dispose_cvars(void *ctx) { qa_cvar_registry_destroy(ctx); }
static void ss_dispose_admission(void *ctx) { qa_shared_pickup_admission_destroy(ctx); }

/* ---- ActorHostRuntime over a B2 clock ---- */
typedef enum ss_clock_kind { SS_CLOCK_FRAME, SS_CLOCK_SELECTED_Q2, SS_CLOCK_SELECTED_Q1, SS_CLOCK_NATIVE_WEAPON } ss_clock_kind;
typedef struct ss_runtime_clock { qa_app_shared_simulation *sim; ss_clock_kind kind; const qa_frame_context *frame; double frame_seconds; } ss_runtime_clock;
/* TS current(): the attached selected Q2 weapon source. */
static qa_ss_selected_weapon_source *ss_selected_q2(qa_app_shared_simulation *sim) {
  if (!sim->has_selected_weapon_source || !qa_ss_is(sim->selected_weapon_source.kind, "q2")) ss_fail("Selected Q2 source is not attached");
  return &sim->selected_weapon_source;
}
static double ss_clock_now(void *ctx) {
  ss_runtime_clock *clock = ctx;
  switch (clock->kind) {
  case SS_CLOCK_SELECTED_Q2: return qa_ss_seconds(ss_selected_q2(clock->sim)->frame.time);
  case SS_CLOCK_SELECTED_Q1: return qa_ss_seconds(qa_app_shared_sim_selected_q1_frame(clock->sim).time);
  case SS_CLOCK_NATIVE_WEAPON: return qa_app_shared_sim_time_seconds(clock->sim);
  default: return qa_ss_seconds(clock->frame->time);
  }
}
static double ss_clock_frame_seconds(void *ctx) {
  ss_runtime_clock *clock = ctx;
  switch (clock->kind) {
  case SS_CLOCK_SELECTED_Q1: return qa_ss_seconds(qa_app_shared_sim_selected_q1_frame(clock->sim).elapsed);
  case SS_CLOCK_SELECTED_Q2: case SS_CLOCK_NATIVE_WEAPON: return clock->frame_seconds;
  default: return qa_ss_seconds(clock->frame->elapsed);
  }
}
static void ss_clock_schedule(void *ctx, qa_owned_actor actor, const double *due) {
  ss_runtime_clock *clock = ctx;
  if (clock->kind == SS_CLOCK_NATIVE_WEAPON) ss_fail("Native weapon callbacks must use their component's source scheduler");
  if (due == NULL) qa_frame_scheduler_cancel(clock->sim->scheduler, actor);
  else qa_app_shared_sim_schedule(clock->sim, actor, *due);
}
static qa_app_sim_actor_host_runtime ss_runtime(qa_app_shared_simulation *sim, qa_numeric_profile numeric, qa_app_sim_source_random *random,
    ss_clock_kind kind, const qa_frame_context *frame, double frame_seconds) {
  ss_runtime_clock *clock = qa_app_shared_sim_keep(sim, sizeof *clock);
  clock->sim = sim; clock->kind = kind; clock->frame = frame; clock->frame_seconds = frame_seconds;
  qa_app_sim_actor_host_runtime runtime = {numeric, random, clock, ss_clock_now, ss_clock_frame_seconds, ss_clock_schedule};
  return runtime;
}
static qa_provider_id ss_campaign(const qa_app_shared_simulation *sim) {
  return sim->recipe.campaign.is_campaign ? sim->recipe.campaign.mission.provider : sim->recipe.map.entities.provider;
}
static qa_q2_mode ss_q2_mode(const qa_app_shared_simulation *sim) {
  return qa_ss_is(sim->options.mode, "deathmatch") ? QA_Q2_MODE_DEATHMATCH : qa_ss_is(sim->options.mode, "coop") ? QA_Q2_MODE_COOP : QA_Q2_MODE_SINGLEPLAYER;
}
static qa_q2_edition ss_q2_edition(const char *edition) { return qa_ss_is(edition, "rerelease") ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC; }
/* The Q2EntityServices options every B2 construction passes. */
static qa_q2_game_options ss_q2_options(qa_app_shared_simulation *sim, qa_provider_id provider, qa_q2_edition edition, qa_provider_id campaign) {
  qa_q2_game_options options; memset(&options, 0, sizeof options);
  options.provider = provider; options.edition = edition; options.map_name = sim->recipe.map.geometry.requested_path;
  options.skill = sim->options.skill; options.mode = ss_q2_mode(sim); options.deathmatch_flags = 0; options.max_clients = sim->options.max_clients;
  options.campaign = campaign; options.combat_provider = sim->recipe.combat.provider;
  options.inventory_provider = sim->recipe.inventory.provider; options.movement_provider = sim->recipe.movement.provider;
  return options;
}
/* A Q2 weapon-hook record: the simulation, the emitting content and its clock. */
typedef struct ss_q2w_record {
  qa_app_shared_simulation *sim; qa_provider_reference source; const qa_frame_context *frame;
  qa_source_damage_modifier modifier; qa_q2_foundation_host host;
} ss_q2w_record;
/* q2SourceDamageHooks() */
static double ss_hook_quad(void *ctx, qa_actor_id actor) { (void)actor; return qa_app_shared_sim_q2_quad_multiplier(((ss_q2w_record *)ctx)->sim); }
static double ss_hook_damage(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_q2_source_damage_multiplier(((ss_q2w_record *)ctx)->sim, actor); }
static void ss_hook_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  qa_app_shared_sim_q2_monster_dodge(((ss_q2w_record *)ctx)->sim, monster, &attacker, eta, &trace);
}
/* this.events.message({ kind: "q2-inventory", counts }, actor) */
static void ss_hook_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) {
  (void)ammo; /* TS ammoChanged: actor => ... */
  qa_app_shared_simulation *sim = ((ss_q2w_record *)ctx)->sim;
  size_t count = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
  double *counts = calloc(count ? count : 1, sizeof *counts);
  if (counts == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) counts[i] = entries[i].count;
  qa_network_event event; memset(&event, 0, sizeof event);
  event.kind = QA_NETWORK_EVENT_Q2_INVENTORY; event.counts = (qa_double_view){counts, count};
  qa_app_sim_simulation_events_message(sim->events, event, true, actor, false, 0);
  free(counts);
}
static bool ss_hook_can_target(void *ctx, const qa_actor_id *attacker, qa_actor_id target) {
  (void)ctx; return attacker == NULL || !qa_same_actor(*attacker, target);
}
/* this.events.emit(content, { kind: "q2-weapon", event }, frame.time) */
static void ss_hook_emit_frame(void *ctx, qa_q2w_event event) {
  ss_q2w_record *record = ctx;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q2-weapon"; input.q2_weapon = &event;
  qa_source_time time = record->frame->time;
  qa_app_shared_sim_emit(record->sim, record->source.content, input, &time, NULL);
}

/* ---- createHandGrenades (3200-3223) ---- */
static double ss_grenade_interval(void *ctx, qa_actor_id actor, double seconds) {
  ss_q2w_record *record = ctx;
  return qa_app_shared_sim_hand_grenade_interval(record->sim, actor, record->source.provider, seconds);
}
static void ss_grenade_noise(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  qa_app_shared_simulation *sim = ((ss_q2w_record *)ctx)->sim;
  sim->strategy->monster_noise(sim, actor, origin, secondary);
}
qa_app_sim_hand_grenade_runtime *qa_app_shared_sim_create_hand_grenades(qa_app_shared_simulation *sim) {
  const qa_hand_grenade_selection *selection = &sim->recipe.equipment.hand_grenades;
  if (!selection->is_enabled) return NULL;
  if (ss_source_is(sim, QA_SS_SOURCE_Q3_QVM)) qa_qvm_primary_weapons_equipment_context(qa_app_shared_sim_require_qvm_weapons(sim), selection->source.provider);
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, selection->source.provider);
  qa_provider_timing world = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider);
  qa_frame_context frame = sim->source_frame; frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0};
  sim->equipment_frame = qa_app_sim_provider_frame(frame, world.clock, timing.clock);
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, qa_ss_is(selection->edition, "rerelease") ? "q2-rerelease" : "classic");
  if (random == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_random, random);
  ss_q2w_record *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->source = selection->source; record->frame = &sim->equipment_frame;
  record->host = qa_app_shared_sim_q2_actor_host(sim, selection->source,
      ss_runtime(sim, world.numeric, random, SS_CLOCK_FRAME, &sim->equipment_frame, 0), NULL, NULL, NULL);
  qa_q2_game_options options = ss_q2_options(sim, selection->source.provider, ss_q2_edition(selection->edition), ss_campaign(sim));
  options.source_damage_modifier = qa_app_shared_sim_damage_modifier_option(sim, &record->modifier);
  qa_q2_entity_services *game = qa_q2_entity_services_create(&record->host, options, NULL, 0);
  if (game == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_q2_game, game);
  qa_q2w_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = record; hooks.quad_multiplier = ss_hook_quad; hooks.source_damage_multiplier = ss_hook_damage;
  hooks.firing_interval = ss_grenade_interval; hooks.emit = ss_hook_emit_frame; hooks.noise = ss_grenade_noise; hooks.dodge = ss_hook_dodge;
  hooks.lag_is_history = false; /* lagCompensation: { kind: "current-world" } */
  hooks.ammo_changed = ss_hook_ammo_changed; hooks.can_target = ss_hook_can_target;
  qa_q2w_ballistics *ballistics = calloc(1, sizeof *ballistics);
  if (ballistics == NULL || !qa_q2w_ballistics_init(ballistics, hooks)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, ballistics); ss_own(sim, ss_dispose_ballistics, ballistics);
  qa_q2_hand_grenade_equipment *equipment = qa_q2_hand_grenade_equipment_create(qa_q2_entity_services_game(game), ballistics);
  if (equipment == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_hand_grenades, equipment);
  qa_app_sim_hand_grenade_runtime *runtime = qa_app_sim_hand_grenade_runtime_create(*selection, equipment, game, random);
  if (runtime == NULL) qa_range_error("Out of memory");
  return runtime;
}

/* ---- createGrapple (3081-3126) ---- */
typedef struct ss_grapple_record {
  qa_app_shared_simulation *sim; qa_grapple_selection selection;
  qa_q1_services *q1_game; qa_q2_foundation_host host;
  qa_app_sim_grapple_slot_host slot_host;
} ss_grapple_record;
/* differentTeam(owner, target) */
static bool ss_different_team(qa_app_shared_simulation *sim, qa_actor_id owner, qa_actor_id target) {
  qa_combat_state_result mine = qa_gameplay_authority_read(sim->combat, owner);
  const char *team = mine.has_value ? mine.value.team : NULL;
  if (team == NULL) return true;
  qa_combat_state_result theirs = qa_gameplay_authority_read(sim->combat, target);
  return !theirs.has_value || !qa_ss_is(theirs.value.team, team);
}
static qa_q1_threewave_input ss_threewave_input(void *ctx, qa_actor_id actor) {
  ss_grapple_record *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  bool held = sim->grapple != NULL && qa_app_sim_grapple_runtime_held(sim->grapple, actor);
  qa_app_sim_weapon_slot *slot = ss_slot(sim, actor);
  qa_q1_threewave_input input; memset(&input, 0, sizeof input);
  input.held = held;
  input.release = !held && (qa_ss_is(record->selection.binding, "offhand") || (slot != NULL && qa_app_sim_weapon_slot_equipment_selected(slot)));
  input.jump = sim->grapple != NULL && qa_app_sim_grapple_runtime_jump(sim->grapple, actor);
  input.view_angles = qa_app_shared_sim_player_view(sim, actor).angles;
  input.teleport_until = ss_source_is(sim, QA_SS_SOURCE_Q1) && player != NULL && player->state.kind == QA_MOVEMENT_STATE_Q1
      ? player->state.as.q1.teleport_time_seconds : 0;
  return input;
}
static qa_vec3 ss_threewave_aim(void *ctx, qa_actor_id actor, qa_vec3 forward) {
  ss_grapple_record *record = ctx;
  qa_owned_actor owner;
  if (!ss_owned(record->sim, actor, &owner)) ss_fail("Hook owner was released");
  return qa_q1_aim(record->q1_game, owner, forward);
}
static qa_q1_threewave_anchor ss_threewave_anchor(void *ctx, qa_actor_id actor) {
  ss_grapple_record *record = ctx;
  const char *anchor = qa_app_shared_sim_grapple_anchor(record->sim, actor);
  qa_client_id client;
  qa_q1_threewave_anchor result = {!qa_ss_is(anchor, "none"), qa_ss_is(anchor, "player") || qa_ss_is(anchor, "corpse") || qa_ss_is(anchor, "box"),
      qa_app_shared_sim_player_client(record->sim, actor, &client)};
  return result;
}
static bool ss_threewave_team(void *ctx, qa_actor_id owner, qa_actor_id target) { return ss_different_team(((ss_grapple_record *)ctx)->sim, owner, target); }
static bool ss_threewave_damage(void *ctx, qa_actor_id target, qa_actor_id owner) { return qa_q1_can_damage(((ss_grapple_record *)ctx)->q1_game, target, owner); }
/* GrappleHooks */
static qa_grapple_pose ss_grapple_pose(void *ctx, qa_actor_id actor, qa_q2_game_services *game) {
  (void)game;
  qa_app_shared_simulation *sim = ((ss_grapple_record *)ctx)->sim;
  qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  qa_grapple_pose pose = {view.angles, "right", view.view_height, player != NULL ? player->gravity_multiplier : 1, {0, 0, -1}};
  return pose;
}
static const char *ss_grapple_hook_anchor(void *ctx, qa_actor_id actor, qa_q2_game_services *game) {
  (void)game; return qa_app_shared_sim_grapple_anchor(((ss_grapple_record *)ctx)->sim, actor);
}
static bool ss_grapple_dead(void *ctx, qa_actor_id actor, qa_q2_game_services *game) {
  (void)game;
  qa_combat_state_result combat = qa_gameplay_authority_read(((ss_grapple_record *)ctx)->sim->combat, actor);
  return combat.has_value && combat.value.can_take_damage && combat.value.health <= 0;
}
static qa_vec3 ss_grapple_previous_velocity(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_grapple_record *)ctx)->sim;
  return sim->grapple != NULL ? qa_app_sim_grapple_runtime_previous_velocity(sim->grapple, actor) : qa_ss_zero;
}
static void ss_grapple_set_previous_velocity(void *ctx, qa_actor_id actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ((ss_grapple_record *)ctx)->sim;
  qa_q2_base_player_state *native = ss_source_is(sim, QA_SS_SOURCE_Q2) ? qa_q2_players_get_state(sim->source.product->players, actor) : NULL;
  if (native != NULL) native->old_velocity = velocity;
  if (sim->grapple != NULL) qa_app_sim_grapple_runtime_set_previous_velocity(sim->grapple, actor, velocity);
}
static double ss_grapple_volume(void *ctx, qa_actor_id actor) {
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  bool has = qa_app_shared_sim_q2_item_weapon_source(((ss_grapple_record *)ctx)->sim, &game, &weapons);
  return (has ? qa_q2w_ballistics_silencer_shots(&weapons->base, actor) : 0) > 0 ? 0.2 : 1;
}
static void ss_grapple_noise(void *ctx, qa_actor_id actor, qa_q2_game_services *services, qa_vec3 origin, const char *kind) {
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  if (qa_app_shared_sim_q2_item_weapon_source(((ss_grapple_record *)ctx)->sim, &game, &weapons))
    qa_q2w_ballistics_player_noise_for_actor(&weapons->base, actor, services, origin, kind);
}
static void ss_grapple_prediction(void *ctx, qa_actor_id actor, bool suppressed) {
  qa_app_shared_simulation *sim = ((ss_grapple_record *)ctx)->sim;
  if (sim->grapple != NULL) qa_app_sim_grapple_runtime_set_prediction(sim->grapple, actor, suppressed);
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player == NULL) return;
  if (player->state.kind == QA_MOVEMENT_STATE_Q2) {
    double flags = player->state.as.q2.flags;
    player->state.as.q2.flags = suppressed ? (double)(qa_js_to_int32(flags) | 64) : (double)(qa_js_to_int32(flags) & ~64);
  } else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    double flags = player->state.as.q2_rerelease.flags;
    player->state.as.q2_rerelease.flags = suppressed ? (double)(qa_js_to_int32(flags) | 64) : (double)(qa_js_to_int32(flags) & ~64);
  }
}
static double ss_grapple_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((ss_grapple_record *)ctx)->sim->physics); }
static void ss_grapple_emit(void *ctx, qa_grapple_cable_event cable) {
  ss_grapple_record *record = ctx;
  qa_q2_composition_event composition; memset(&composition, 0, sizeof composition);
  qa_q2_ctf_event ctf; memset(&ctf, 0, sizeof ctf);
  qa_lmctf_event lmctf; memset(&lmctf, 0, sizeof lmctf);
  if (qa_ss_is(record->selection.mechanic, "q2-lmctf")) {
    lmctf.kind = cable.kind; lmctf.actor = cable.actor; lmctf.start = cable.start; lmctf.end = cable.end; lmctf.offset = cable.offset;
    composition.kind = QA_Q2_COMPOSITION_EVENT_LMCTF; composition.lmctf = &lmctf;
  } else {
    ctf.kind = cable.kind; ctf.actor = cable.actor; ctf.start = cable.start; ctf.end = cable.end; ctf.offset = cable.offset;
    composition.kind = QA_Q2_COMPOSITION_EVENT_CTF; composition.ctf = &ctf;
  }
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q2-composition"; input.q2_composition = &composition;
  qa_source_time time = record->sim->grapple_frame.time;
  qa_app_shared_sim_emit(record->sim, record->selection.source.content, input, &time, NULL);
}
static bool ss_ctf_can_damage(void *ctx, qa_actor_id owner, qa_actor_id target) { return ss_different_team(((ss_grapple_record *)ctx)->sim, owner, target); }
static bool ss_lmctf_can_attach(void *ctx, qa_actor_id owner, qa_actor_id target, qa_q2_game_services *game) {
  (void)game; return ss_different_team(((ss_grapple_record *)ctx)->sim, owner, target);
}
static bool ss_lmctf_can_damage(void *ctx, qa_actor_id target) { (void)ctx; (void)target; return true; }
static bool ss_lmctf_player_hit(void *ctx, qa_actor_id target, qa_q2_game_services *game) {
  (void)game; qa_client_id client; return qa_app_shared_sim_player_client(((ss_grapple_record *)ctx)->sim, target, &client);
}
static void ss_lmctf_released(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_grapple_record *)ctx)->sim;
  if (sim->grapple != NULL) qa_app_sim_grapple_runtime_released(sim->grapple, actor);
}
qa_app_sim_grapple_runtime *qa_app_shared_sim_create_grapple(qa_app_shared_simulation *sim) {
  const qa_grapple_selection *selection = &sim->recipe.equipment.grapple;
  if (!selection->is_enabled) return NULL;
  if (qa_ss_is(selection->mechanic, "q3-qvm")) return NULL;
  qa_provider_timing world = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider);
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, selection->source.provider);
  qa_frame_context frame = sim->source_frame; frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0};
  sim->grapple_frame = qa_app_sim_provider_frame(frame, world.clock, timing.clock);
  bool threewave = qa_ss_is(selection->mechanic, "q1-threewave");
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed,
      qa_ss_is(selection->edition, "rerelease") && !threewave ? "q2-rerelease" : "classic");
  if (random == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_random, random);
  qa_app_sim_actor_host_runtime runtime = ss_runtime(sim, world.numeric, random, SS_CLOCK_FRAME, &sim->grapple_frame, 0);
  ss_grapple_record *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->selection = *selection;
  record->slot_host = qa_app_shared_sim_grapple_slot_host(sim);
  qa_app_sim_grapple_source source; memset(&source, 0, sizeof source);
  source.kind = selection->mechanic;
  if (threewave) {
    qa_q1_options options; memset(&options, 0, sizeof options);
    options.provider = selection->source.provider; options.edition = selection->edition;
    options.physics_edition = qa_app_shared_sim_q1_physics_edition(sim);
    options.skill = sim->options.skill; options.deathmatch = qa_ss_is(sim->options.mode, "deathmatch") ? 1 : 0;
    options.coop = qa_ss_is(sim->options.mode, "coop"); options.max_clients = sim->options.max_clients; options.has_max_clients = true;
    options.campaign = ss_campaign(sim); options.combat_provider = sim->recipe.combat.provider;
    options.inventory_provider = sim->recipe.inventory.provider; options.movement_provider = sim->recipe.movement.provider;
    options.gravity = qa_app_sim_shared_physics_gravity(sim->physics);
    record->q1_game = calloc(1, sizeof *record->q1_game);
    if (record->q1_game == NULL || !qa_q1_services_init(record->q1_game, qa_app_shared_sim_q1_actor_host(sim, selection->source, runtime, NULL), options))
      qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_free, record->q1_game); ss_own(sim, ss_dispose_q1_game, record->q1_game);
    qa_q1_begin_frame(record->q1_game, qa_ss_seconds(sim->grapple_frame.time), qa_ss_seconds(sim->grapple_frame.elapsed));
    qa_q1_threewave_host host = {record, ss_threewave_input, ss_threewave_aim, ss_threewave_anchor, ss_threewave_team, ss_threewave_team, ss_threewave_damage};
    qa_q1_threewave_grapple *core = calloc(1, sizeof *core);
    if (core == NULL || !qa_q1_threewave_grapple_init(core, record->q1_game, host)) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_free, core); ss_own(sim, ss_dispose_threewave, core);
    source.q1_game = record->q1_game; source.q1_core = core;
  } else {
    record->host = qa_app_shared_sim_q2_actor_host(sim, selection->source, runtime, NULL, NULL, NULL);
    qa_q2_entity_services *game = qa_q2_entity_services_create(&record->host,
        ss_q2_options(sim, selection->source.provider, ss_q2_edition(selection->edition), ss_campaign(sim)), NULL, 0);
    if (game == NULL) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_q2_game, game);
    qa_grapple_hooks hooks = {record, ss_grapple_pose, ss_grapple_hook_anchor, ss_grapple_dead, ss_grapple_previous_velocity,
        ss_grapple_set_previous_velocity, ss_grapple_volume, ss_grapple_noise, ss_grapple_prediction, ss_grapple_gravity, ss_grapple_emit};
    source.q2_game = game;
    if (qa_ss_is(selection->mechanic, "q2-ctf")) {
      source.ctf_core = qa_q2_ctf_grapple_create(hooks, ss_ctf_can_damage, record, NULL, NULL);
      if (source.ctf_core == NULL) qa_range_error("Out of memory");
      ss_own(sim, ss_dispose_ctf_grapple, source.ctf_core);
    } else {
      qa_lmctf_grapple_policy policy = {record, ss_lmctf_can_attach, ss_lmctf_can_damage, ss_lmctf_player_hit};
      source.lmctf_core = qa_lmctf_grapple_create(hooks, policy, ss_lmctf_released, record);
      if (source.lmctf_core == NULL) qa_range_error("Out of memory");
      ss_own(sim, ss_dispose_lmctf_grapple, source.lmctf_core);
    }
  }
  qa_app_sim_grapple_runtime *grapple = qa_app_sim_grapple_runtime_create(*selection, source, random, &record->slot_host);
  if (grapple == NULL) qa_range_error("Out of memory");
  return grapple;
}

/* ---- initializeQvmGrapple (3128-3196) ---- */
/* isDeepStrictEqual over GrappleSelection: SameValue for numbers. */
static bool ss_same_number(double a, double b) { return a == b ? (a != 0 || signbit(a) == signbit(b)) : (isnan(a) && isnan(b)); }
static bool ss_same_text(const char *a, const char *b) { return a == NULL || b == NULL ? a == b : strcmp(a, b) == 0; }
static bool ss_same_vec3(qa_vec3 a, qa_vec3 b) { return ss_same_number(a.x, b.x) && ss_same_number(a.y, b.y) && ss_same_number(a.z, b.z); }
static bool ss_same_grapple_profile(const qa_qvm_grapple_definition *a, const qa_qvm_grapple_definition *b) {
  if (a == NULL || b == NULL) return a == b;
  if (!ss_same_text(a->id, b->id) || !ss_same_text(a->title, b->title) || !ss_same_text(a->module.id, b->module.id)
      || !ss_same_text(a->module.artifact_path, b->module.artifact_path) || !ss_same_text(a->module.digest, b->module.digest)
      || !ss_same_text(a->module.revision, b->module.revision) || a->abi_profile != b->abi_profile
      || !ss_same_number(a->entity_stride, b->entity_stride) || !ss_same_number(a->client_stride, b->client_stride)) return false;
  const qa_qvm_grapple_fields *f = &a->fields, *g = &b->fields;
  if (!ss_same_number(f->inuse, g->inuse) || !ss_same_number(f->client, g->client) || !ss_same_number(f->parent, g->parent)
      || !ss_same_number(f->target, g->target) || f->has_mover != g->has_mover || (f->has_mover && !ss_same_number(f->mover, g->mover))
      || !ss_same_number(f->hook, g->hook) || !ss_same_number(f->health, g->health) || !ss_same_number(f->takedamage, g->takedamage)
      || !ss_same_number(f->event_time, g->event_time) || !ss_same_number(f->free_after_event, g->free_after_event)) return false;
  const qa_qvm_grapple_globals *h = &a->globals, *k = &b->globals;
  if (!ss_same_number(h->time, k->time) || !ss_same_number(h->frame, k->frame) || !ss_same_number(h->movement, k->movement)
      || !ss_same_number(h->forward, k->forward) || !ss_same_number(h->ground_plane, k->ground_plane)) return false;
  const qa_qvm_grapple_callbacks *c = &a->callbacks, *d = &b->callbacks;
  if (!ss_same_number(c->allocate, d->allocate) || !ss_same_number(c->free, d->free) || !ss_same_number(c->fire, d->fire)
      || !ss_same_number(c->release, d->release) || !ss_same_number(c->force_release, d->force_release) || !ss_same_number(c->missile, d->missile)
      || c->has_follow != d->has_follow || (c->has_follow && !ss_same_number(c->follow, d->follow))
      || !ss_same_number(c->think, d->think) || !ss_same_number(c->pull, d->pull)
      || c->has_move_mover_hooks != d->has_move_mover_hooks || (c->has_move_mover_hooks && !ss_same_number(c->move_mover_hooks, d->move_mover_hooks))
      || !ss_same_number(c->damage, d->damage) || !ss_same_number(c->same_team, d->same_team) || !ss_same_number(c->player_move, d->player_move))
    return false;
  if (a->fire_arguments.count != b->fire_arguments.count) return false;
  for (size_t i = 0; i < a->fire_arguments.count; i++) if (!ss_same_number(a->fire_arguments.values[i], b->fire_arguments.values[i])) return false;
  if (!ss_same_number(a->movement.byte_length, b->movement.byte_length) || a->movement.word_count != b->movement.word_count) return false;
  for (size_t i = 0; i < a->movement.word_count; i++)
    if (!ss_same_number(a->movement.words[i].offset, b->movement.words[i].offset) || !ss_same_number(a->movement.words[i].value, b->movement.words[i].value)) return false;
  if (a->initial_cvar_count != b->initial_cvar_count) return false;
  for (size_t i = 0; i < a->initial_cvar_count; i++)
    if (!ss_same_text(a->initial_cvars[i].name, b->initial_cvars[i].name) || !ss_same_text(a->initial_cvars[i].value, b->initial_cvars[i].value)) return false;
  if (!ss_same_number(a->event_lifetime_milliseconds, b->event_lifetime_milliseconds) || !ss_same_number(a->grapple_damage_method, b->grapple_damage_method)
      || !ss_same_number(a->pulling_flag, b->pulling_flag)) return false;
  const qa_qvm_grapple_presentation *p = &a->presentation, *q = &b->presentation;
  if (!ss_same_text(p->projectile_model, q->projectile_model) || !ss_same_text(p->view_model, q->view_model) || !ss_same_number(p->weapon_index, q->weapon_index)
      || !ss_same_text(p->view_anchor.path, q->view_anchor.path) || !ss_same_text(p->view_anchor.tag, q->view_anchor.tag)
      || !ss_same_vec3(p->view_anchor.offset, q->view_anchor.offset) || !ss_same_number(p->view_anchor.fov_offset_above, q->view_anchor.fov_offset_above)
      || !ss_same_number(p->view_anchor.fov_offset_scale, q->view_anchor.fov_offset_scale) || p->view_attachment_count != q->view_attachment_count) return false;
  for (size_t i = 0; i < p->view_attachment_count; i++)
    if (!ss_same_text(p->view_attachments[i].path, q->view_attachments[i].path) || !ss_same_text(p->view_attachments[i].tag, q->view_attachments[i].tag)) return false;
  return p->cable.is_shader == q->cable.is_shader && ss_same_text(p->cable.path, q->cable.path) && ss_same_number(p->cable.width, q->cable.width)
      && ss_same_text(p->cable.flight, q->cable.flight) && ss_same_text(p->cable.pull, q->cable.pull) && ss_same_text(p->cable.hold, q->cable.hold)
      && ss_same_number(p->cable.segment_length, q->cable.segment_length) && ss_same_text(p->fire_sound, q->fire_sound)
      && ss_same_text(p->attach_sound, q->attach_sound) && ss_same_text(p->release_sound, q->release_sound)
      && ss_same_text(p->pull_sound, q->pull_sound) && ss_same_text(p->hang_sound, q->hang_sound);
}
static bool ss_same_grapple_selection(const qa_grapple_selection *a, const qa_grapple_selection *b) {
  return a->is_enabled == b->is_enabled && ss_same_text(a->source.provider, b->source.provider) && ss_same_text(a->source.content, b->source.content)
      && ss_same_text(a->binding, b->binding) && ss_same_text(a->mechanic, b->mechanic) && ss_same_text(a->edition, b->edition)
      && ss_same_grapple_profile(a->profile, b->profile);
}
typedef struct ss_qvm_grapple_record {
  qa_app_shared_simulation *sim; qa_grapple_selection selection;
  qa_app_q3_qvm_server_game *primary; bool has_primary_combat; qa_qvm_game_combat primary_combat;
  qa_app_sim_qvm_grapple_target *targets; size_t target_count, target_capacity;
  char (*userinfo)[512];
  qa_actor_id attackers[2];
  qa_app_sim_grapple_slot_host slot_host;
} ss_qvm_grapple_record;
static double ss_qvm_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) {
  const qa_app_sim_real_time *clock = ((ss_qvm_grapple_record *)ctx)->sim->options.weapon_behavior_real_time;
  return clock->real_time(clock->ctx, has_output, out);
}
static void ss_qvm_assert_current(void *ctx) { qa_app_shared_sim_assert_open(((ss_qvm_grapple_record *)ctx)->sim); }
static void ss_qvm_grapple_event(void *ctx, qa_app_q3_source_event event) {
  ss_qvm_grapple_record *record = ctx;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q3-source"; input.q3_source = &event;
  qa_source_time time = record->sim->grapple_frame.time;
  qa_app_shared_sim_emit(record->sim, record->selection.source.content, input, &time, NULL);
}
static void ss_qvm_grapple_damage(void *ctx, qa_app_sim_qvm_grapple_damage hit) {
  ss_qvm_grapple_record *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_damage_request request; memset(&request, 0, sizeof request);
  request.target = hit.target; request.amount = hit.amount; request.knockback = (qa_js_to_int32(hit.flags) & 4) != 0 ? 0 : hit.amount;
  request.direction = hit.direction; request.point = hit.point; request.normal = qa_ss_zero; request.delivery = "direct";
  request.attack.sequence = sim->attack_sequence++; request.attack.time = sim->grapple_frame.time;
  if (hit.attacker != NULL) { record->attackers[0] = *hit.attacker; request.attack.attacker = &record->attackers[0]; }
  if (hit.inflictor != NULL) { record->attackers[1] = *hit.inflictor; request.attack.inflictor = &record->attackers[1]; }
  request.attack.weapon = "q3:weapon_grapplinghook"; request.attack.weapon_provider = record->selection.source.provider;
  request.attack.combat_provider = sim->recipe.combat.provider; request.attack.inventory_provider = sim->recipe.inventory.provider;
  request.attack.movement_provider = sim->recipe.movement.provider;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q3; request.attack.cause.means_of_death = hit.method; request.attack.cause.damage_flags = hit.flags;
  qa_gameplay_authority_apply(sim->combat, request, NULL, NULL);
}
static void ss_qvm_grapple_velocity(void *ctx, qa_actor_id actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ((ss_qvm_grapple_record *)ctx)->sim;
  qa_owned_actor owner; qa_body_state body;
  bool owned = ss_owned(sim, actor, &owner);
  bool has_body = qa_app_shared_sim_body(sim, actor, &body);
  if (!owned || !has_body) return;
  if (body.velocity.x == velocity.x && body.velocity.y == velocity.y && body.velocity.z == velocity.z) return;
  if (ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE)) qa_ss_map_set(&sim->native_equipment_velocity, &actor, &velocity);
  else { body.velocity = velocity; body.ground = NULL; qa_shared_body_table_write(ss_bodies(sim), owner, body); }
  if (ss_source_is(sim, QA_SS_SOURCE_Q3_QVM)) {
    qa_app_q3_player native;
    if (ss_qvm_player_of(sim, actor, &native)) {
      qa_qvm_game_data *data = qa_app_q3_guest_records_data(ss_qvm_records(sim));
      qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
      qa_qvm_game_data_copy_player_state(data, native.source_entity, &state, stats, persistent, powerups, ammo);
      state.velocity = velocity; state.ground_entity_number = 1023;
      qa_qvm_game_data_write_player_state(data, native.source_entity, &state);
    }
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (player != NULL) {
    player->ground.kind = QA_TRACE_HIT_NONE;
    if (player->state.kind == QA_MOVEMENT_STATE_Q2) {
      qa_integer_triple eighths = {{trunc(velocity.x * 8), trunc(velocity.y * 8), trunc(velocity.z * 8)}};
      player->state.as.q2.velocity_eighths = eighths;
    } else if (player->state.kind == QA_MOVEMENT_STATE_Q1) player->state.as.q1.velocity = velocity;
    else if (player->state.kind == QA_MOVEMENT_STATE_QW) player->state.as.qw.velocity = velocity;
    else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.velocity = velocity;
    else player->state.as.q3.velocity = velocity;
  }
}
static const char *ss_qvm_team(double team) { return team == 1 ? "red" : team == 2 ? "blue" : team == 3 ? "spectator" : "free"; }
static const qa_app_sim_qvm_grapple_target *ss_qvm_grapple_targets(void *ctx, size_t *out_count) {
  ss_qvm_grapple_record *record = ctx; qa_app_shared_simulation *sim = record->sim;
  size_t observation_count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  record->targets = ss_grow(record->targets, &record->target_capacity, observation_count + 1, sizeof *record->targets);
  record->userinfo = realloc(record->userinfo, (observation_count + 1) * sizeof *record->userinfo);
  if (record->userinfo == NULL) qa_range_error("Out of memory");
  qa_actor_id world; bool has_world = qa_app_shared_sim_world_actor(sim, &world);
  size_t count = 0;
  for (size_t i = 0; i < observation_count; i++) {
    const qa_actor_observation *observation = &observations[i];
    if (qa_ss_is(observation->owner, record->selection.source.provider) || (has_world && qa_same_actor(world, observation->id))) continue;
    qa_actor_id actor = observation->id;
    qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, actor, &body);
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
    if (!has_body) continue;
    double slot; bool has_slot = record->primary != NULL && qa_app_q3_guest_records_slot(qa_app_q3_qvm_server_game_records(record->primary), actor, &slot);
    bool has_native = false, native_null = false; qa_qvm_game_damage_state native;
    if (has_slot && record->has_primary_combat) { has_native = qa_qvm_game_combat_state(&record->primary_combat, slot, &native); native_null = !has_native; }
    if (native_null) continue;
    qa_app_sim_qvm_grapple_target *target = &record->targets[count];
    memset(target, 0, sizeof *target);
    target->actor = actor;
    qa_client_id client;
    const char *userinfo = qa_app_shared_sim_source_player_userinfo(sim, actor);
    if (player == NULL && qa_app_shared_sim_player_client(sim, actor, &client)
        && (ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE) || ss_source_is(sim, QA_SS_SOURCE_Q3_QVM))) {
      qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
      bool qvm = ss_source_is(sim, QA_SS_SOURCE_Q3_QVM);
      ss_qvm_state state; if (qvm) ss_qvm_actor_state(sim, actor, &state);
      target->body = body; target->body.angles = view.angles;
      target->health = has_native ? native.health : qvm ? state.stats[0] : qa_app_shared_sim_native_player_ui(sim, actor).health;
      target->is_player = true; target->view_height = view.view_height;
      target->team = qvm ? ss_qvm_team(state.persistent[3]) : "free";
      snprintf(record->userinfo[count], sizeof record->userinfo[0], "%s", userinfo != NULL ? userinfo : "\\name\\Player\\ip\\localhost\\model\\sarge/default");
      target->userinfo = record->userinfo[count];
      count++;
      continue;
    }
    if (player == NULL) {
      target->body = body; target->health = has_native ? native.health : combat.has_value ? combat.value.health : 0;
      target->is_player = false; target->mover = qa_ss_is(qa_app_shared_sim_grapple_anchor(sim, actor), "brush");
      count++;
      continue;
    }
    char team[64] = "";
    if (combat.has_value && combat.value.team != NULL) {
      snprintf(team, sizeof team, "%s", combat.value.team);
      for (char *c = team; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }
    target->body = body; target->body.angles = player->view_angles; target->health = combat.has_value ? combat.value.health : 0;
    target->is_player = true; target->view_height = player->view_height;
    snprintf(record->userinfo[count], sizeof record->userinfo[0], "%s",
        userinfo != NULL ? userinfo : "\\name\\Player\\ip\\localhost\\model\\sarge/default\\handicap\\100");
    target->userinfo = record->userinfo[count];
    target->team = qa_ss_is(team, "team:red") ? "red" : qa_ss_is(team, "team:blue") ? "blue" : qa_ss_is(team, "red") ? "red"
        : qa_ss_is(team, "blue") ? "blue" : "free";
    count++;
  }
  *out_count = count;
  return record->targets;
}
static void ss_dispose_qvm_grapple_record(void *ctx) {
  ss_qvm_grapple_record *record = ctx;
  free(record->targets); free(record->userinfo);
  if (record->has_primary_combat) qa_qvm_game_combat_destroy(&record->primary_combat);
}
void qa_app_shared_sim_initialize_qvm_grapple(qa_app_shared_simulation *sim) {
  const qa_grapple_selection *selection = &sim->recipe.equipment.grapple;
  if (!selection->is_enabled || !qa_ss_is(selection->mechanic, "q3-qvm")) return;
  const qa_app_prepared_qvm_grapple *prepared = sim->options.prepared_qvm_grapple;
  const qa_app_sim_real_time *real_time = sim->options.weapon_behavior_real_time;
  if (prepared == NULL || !ss_same_grapple_selection(&prepared->selection, selection) || real_time == NULL)
    ss_fail("Selected QVM grapple requires its exact prepared source and host clock");
  ss_qvm_grapple_record *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->selection = *selection;
  record->primary = ss_source_is(sim, QA_SS_SOURCE_Q3_QVM) ? sim->source.qvm : NULL;
  const qa_app_sim_q3_guest_options *guest = sim->options.q3_guest;
  const qa_resolved_qvm_artifact *primary_artifact = guest != NULL ? &guest->prepared->artifact : NULL;
  const qa_qvm_primary_combat_profile *primary_profile = guest != NULL ? qa_app_prepared_q3_primary_combat(guest->prepared) : NULL;
  if (record->primary != NULL && primary_artifact != NULL && primary_profile != NULL) {
    if (!qa_qvm_game_combat_init(&record->primary_combat, qa_app_q3_qvm_server_game_game(record->primary), primary_artifact, primary_profile->base))
      qa_range_error("Out of memory");
    record->has_primary_combat = true;
  }
  ss_own(sim, ss_dispose_qvm_grapple_record, record);
  qa_app_sim_qvm_grapple_source_options options; memset(&options, 0, sizeof options);
  options.artifact = &prepared->artifact; options.profile = selection->profile; options.provider = selection->source.provider;
  options.mounts = prepared->mounts; options.actors = sim->actors; options.bodies = ss_bodies(sim); options.scene = sim->scene;
  options.context.session = sim->session; options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  options.seed = sim->options.seed; options.entity_text = sim->world_entities; options.binding = selection->binding;
  options.ctx = record; options.real_time = ss_qvm_real_time; options.assert_current = ss_qvm_assert_current;
  options.event = ss_qvm_grapple_event; options.damage = ss_qvm_grapple_damage; options.velocity = ss_qvm_grapple_velocity;
  options.targets = ss_qvm_grapple_targets;
  qa_app_sim_qvm_grapple_source *game = qa_app_sim_qvm_grapple_source_create(options);
  if (game == NULL) qa_range_error("Out of memory");
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, "classic");
  if (random == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_random, random);
  qa_app_sim_grapple_source source; memset(&source, 0, sizeof source);
  source.kind = "q3-qvm"; source.q3_game = game; source.q3_core = qa_app_sim_qvm_grapple_source_core(game);
  record->slot_host = qa_app_shared_sim_grapple_slot_host(sim);
  sim->grapple = qa_app_sim_grapple_runtime_create(*selection, source, random, &record->slot_host);
  if (sim->grapple == NULL) qa_range_error("Out of memory");
  bool has_saved = sim->has_pending_qvm_grapple_restore;
  sim->has_pending_qvm_grapple_restore = false;
  if (!has_saved) {
    size_t count = 0;
    const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
    qa_actor_id *actors = malloc((count ? count : 1) * sizeof *actors);
    if (actors == NULL) qa_range_error("Out of memory");
    memcpy(actors, players, count * sizeof *actors);
    for (size_t i = 0; i < count; i++) {
      if (sim->strategy->owns_equipment_players(sim)) qa_app_shared_sim_require_equipment_player(sim, actors[i]);
      else qa_app_shared_sim_admit_grapple(sim, actors[i]);
    }
    free(actors);
    return;
  }
  qa_app_sim_grapple_runtime_restore(sim->grapple, &sim->pending_qvm_grapple_restore_grapple);
  if (sim->strategy->owns_equipment_players(sim))
    for (size_t i = 0; i < sim->pending_qvm_grapple_restore_grapple.control_count; i++) {
      qa_actor_id actor = qa_session_actor_registry_reference_saved(sim->actors, sim->pending_qvm_grapple_restore_grapple.controls[i].actor, "checkpoint");
      qa_ss_map_set(&sim->native_equipment_players, &actor, NULL);
    }
  for (size_t i = 0; i < sim->pending_qvm_grapple_restore_slot_count; i++) {
    const qa_app_sim_saved_weapon_slot *entry = &sim->pending_qvm_grapple_restore_slots[i];
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(sim->actors, entry->actor, &actor)) ss_fail("Saved grapple slot actor is missing");
    qa_app_shared_sim_bind_weapon_slot_state(sim, actor.id, &entry->state);
  }
}

/* ---- createQvmWeaponBehavior (3258-3278) ---- */
typedef struct ss_weapon_behavior_record {
  qa_app_shared_simulation *sim; bool team_mode;
  qa_app_sim_qvm_weapon_target *targets; size_t target_count, target_capacity;
  char (*userinfo)[512];
  qa_provider_reference reference; qa_q2_foundation_host engine; qa_localization_table localization; qa_provider_timing timing;
} ss_weapon_behavior_record;
static void ss_dispose_weapon_behavior_record(void *ctx) { ss_weapon_behavior_record *record = ctx; free(record->targets); free(record->userinfo); }
static double ss_behavior_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) {
  const qa_app_sim_real_time *clock = ((ss_weapon_behavior_record *)ctx)->sim->options.weapon_behavior_real_time;
  return clock->real_time(clock->ctx, has_output, out);
}
static void ss_behavior_assert_current(void *ctx) { qa_app_shared_sim_assert_open(((ss_weapon_behavior_record *)ctx)->sim); }
static void ss_behavior_print(void *ctx, const char *text) { qa_app_shared_sim_print(((ss_weapon_behavior_record *)ctx)->sim, text, NULL); }
static const qa_app_sim_qvm_weapon_target *ss_behavior_targets(void *ctx, size_t *out_count) {
  ss_weapon_behavior_record *record = ctx; qa_app_shared_simulation *sim = record->sim;
  size_t observation_count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  record->targets = ss_grow(record->targets, &record->target_capacity, observation_count + 1, sizeof *record->targets);
  record->userinfo = realloc(record->userinfo, (observation_count + 1) * sizeof *record->userinfo);
  if (record->userinfo == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < observation_count; i++) {
    qa_actor_id actor = observations[i].id;
    qa_body_state body;
    bool has_body = qa_app_shared_sim_body(sim, actor, &body);
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
    if (!has_body) continue;
    qa_app_sim_qvm_weapon_target *target = &record->targets[count];
    memset(target, 0, sizeof *target);
    target->actor = actor; target->body = body; target->health = combat.has_value ? combat.value.health : 0;
    qa_client_id client;
    if (!qa_app_shared_sim_player_client(sim, actor, &client)) { target->kind = "actor"; count++; continue; }
    const char *source_userinfo = qa_app_shared_sim_source_player_userinfo(sim, actor);
    snprintf(record->userinfo[count], sizeof record->userinfo[0], "%s", source_userinfo != NULL ? source_userinfo : "");
    char scratch[256];
    const char *spectator = qa_q2_userinfo_get(record->userinfo[count], "spectator", scratch, sizeof scratch);
    bool is_spectator = spectator != NULL && strcmp(spectator, "1") == 0;
    const char *info_team = is_spectator ? NULL : qa_q2_userinfo_get(record->userinfo[count], "team", scratch, sizeof scratch);
    is_spectator = is_spectator || (info_team != NULL && (strcmp(info_team, "spectator") == 0 || strcmp(info_team, "s") == 0));
    char source_team[64] = "";
    if (combat.has_value && combat.value.team != NULL) {
      snprintf(source_team, sizeof source_team, "%s", combat.value.team);
      for (char *c = source_team; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }
    const char *team = is_spectator ? "spectator" : !record->team_mode ? "free"
        : qa_ss_is(source_team, "red") || qa_ss_is(source_team, "team:red") ? "red"
        : qa_ss_is(source_team, "blue") || qa_ss_is(source_team, "team:blue") ? "blue" : NULL;
    if (team == NULL) ss_fail("QVM weapon component cannot represent source team %s",
        combat.has_value && combat.value.team != NULL ? combat.value.team : "unassigned");
    target->kind = "player"; target->userinfo = record->userinfo[count]; target->team = team;
    count++;
  }
  *out_count = count;
  return record->targets;
}
qa_app_sim_qvm_weapon_behavior_source *qa_app_shared_sim_create_qvm_weapon_behavior(qa_app_shared_simulation *sim, qa_app_prepared_weapon_behavior entry) {
  if (sim->options.weapon_behavior_real_time == NULL) ss_fail("QVM weapon components require host calendar services");
  const qa_app_prepared_qvm_weapon_behavior *prepared = entry.payload;
  ss_weapon_behavior_record *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->team_mode = qa_app_shared_sim_team_game(sim);
  ss_own(sim, ss_dispose_weapon_behavior_record, record);
  qa_app_sim_qvm_weapon_behavior_options options; memset(&options, 0, sizeof options);
  options.artifact = prepared->artifact; options.profile = prepared->profile; options.mounts = prepared->mounts;
  options.scene = sim->scene; options.context.session = sim->session; options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  options.seed = sim->options.seed; options.entity_text = sim->world_entities; options.mode = sim->options.mode;
  options.team_mode = record->team_mode; options.ctx = record; options.real_time = ss_behavior_real_time;
  options.assert_current = ss_behavior_assert_current; options.print = ss_behavior_print; options.targets = ss_behavior_targets;
  qa_app_sim_qvm_weapon_behavior_source *source = qa_app_sim_qvm_weapon_behavior_source_create(options);
  if (source == NULL) qa_range_error("Out of memory");
  return source;
}

/* ---- createNativeWeaponBehavior (3280-3311) ---- */
static void ss_dispose_timing(void *ctx) { qa_native_provider_timing_free(ctx); }
static noreturn void ss_native_unsupported(void) { ss_fail("Native weapon component cannot mutate primary world ownership"); }
static void ss_native_admit(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) { (void)ctx; (void)record; (void)actor; ss_native_unsupported(); }
static void ss_native_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) { (void)ctx; (void)actor; (void)collision; ss_native_unsupported(); }
static const char *const *ss_native_command(void *ctx, size_t *out_count, const char **out_args) {
  (void)ctx; *out_count = 0; *out_args = ""; return NULL; /* () => ({ arguments: [], args: "" }) */
}
static void ss_native_add_command(void *ctx, const char *text) { (void)ctx; (void)text; ss_native_unsupported(); }
static void ss_native_debug_graph(void *ctx, double value, double color) { (void)ctx; (void)value; (void)color; ss_native_unsupported(); }
static void ss_native_debug_shapes(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event) { (void)ctx; (void)event; ss_native_unsupported(); }
static void ss_native_world_text(void *ctx, qa_compat_q2_rerelease_world_text_event event) { (void)ctx; (void)event; ss_native_unsupported(); }
static char *ss_native_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  char *text = qa_app_q2_localized_text(&((ss_weapon_behavior_record *)ctx)->localization, key, arguments, count);
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}
static uint8_t *ss_native_localization_read(void *ctx, const char *path, size_t *out_length) {
  qa_content_opened_resource opened;
  *out_length = 0;
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
static qa_app_sim_rerelease_weapon_actor ss_native_weapon_actor(void *ctx, qa_actor_id id) {
  qa_app_shared_simulation *sim = ((ss_weapon_behavior_record *)ctx)->sim;
  qa_owned_actor actor; qa_body_state body;
  bool owned = ss_owned(sim, id, &actor), has_body = qa_app_shared_sim_body(sim, id, &body);
  if (!owned || !has_body) ss_fail("Native weapon component references an unavailable shared actor");
  qa_client_id client;
  bool player = qa_app_shared_sim_player_client(sim, id, &client);
  qa_app_sim_player_view view; if (player) view = qa_app_shared_sim_player_view(sim, id);
  const char *userinfo = player ? qa_app_shared_sim_source_player_userinfo(sim, id) : "";
  qa_app_sim_rerelease_weapon_actor result; memset(&result, 0, sizeof result);
  result.actor = actor; result.userinfo = userinfo != NULL ? userinfo : "";
  result.shooter.body = body; result.shooter.view_angles = player ? view.angles : body.angles; result.shooter.view_height = player ? view.view_height : 0;
  return result;
}
static void ss_native_behavior_print(void *ctx, const char *text) { qa_app_shared_sim_print(((ss_weapon_behavior_record *)ctx)->sim, text, NULL); }
qa_app_sim_rerelease_weapon_behavior_source *qa_app_shared_sim_create_native_weapon_behavior(qa_app_shared_simulation *sim,
    qa_app_prepared_weapon_behavior entry, void (*next_frame)(void *frame_ctx), void *frame_ctx) {
  const qa_windows_capabilities *clock = sim->options.weapon_behavior_clock;
  if (clock == NULL) ss_fail("Native weapon components require host clock capabilities");
  const qa_app_prepared_native_weapon_behavior *prepared = entry.payload;
  ss_weapon_behavior_record *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim; record->reference = entry.selection.source;
  if (!qa_native_provider_timing(record->reference, QA_GAME_FAMILY_Q2, true, &record->timing)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_timing, &record->timing);
  const qa_provider_timing timing = record->timing;
  qa_cvar_registry_options cvar_options; memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q2_RERELEASE;
  cvar_options.context.session = sim->session; cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  cvar_options.print = ss_native_behavior_print; cvar_options.print_ctx = record;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  if (cvars == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_cvars, cvars);
  char clients[32], skill[32], gravity[32];
  const char *names[] = {"maxclients", "skill", "deathmatch", "coop", "sv_gravity"};
  const char *values[] = {qa_ss_number_text(sim->options.max_clients, clients), qa_ss_number_text(sim->options.skill, skill),
      qa_ss_is(sim->options.mode, "deathmatch") ? "1" : "0", qa_ss_is(sim->options.mode, "coop") ? "1" : "0",
      qa_ss_number_text(qa_app_sim_shared_physics_gravity(sim->physics), gravity)};
  for (size_t i = 0; i < 5; i++) { qa_cvar_snapshot snapshot; qa_cvar_registry_register(cvars, names[i], values[i], 0, &snapshot); }
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, "q2-rerelease");
  if (random == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_random, random);
  qa_app_sim_actor_host_runtime runtime = ss_runtime(sim, timing.numeric, random, SS_CLOCK_NATIVE_WEAPON, NULL, 0.025);
  qa_load_server_localization_resources("english", ss_native_localization_read, prepared->mounts, QA_LOC_Q2_RERELEASE, &record->localization);
  ss_own(sim, ss_dispose_localization, &record->localization);
  ss_own(sim, ss_dispose_weapon_behavior_record, record);
  record->engine = qa_app_shared_sim_q2_actor_host(sim, record->reference, runtime, NULL, NULL, NULL);
  qa_app_sim_rerelease_weapon_behavior_options options; memset(&options, 0, sizeof options);
  options.prepared = prepared->prepared; options.declaration = prepared->declaration;
  options.clock_ctx = clock->ctx; options.now_milliseconds = clock->now_milliseconds; options.performance_counter = clock->performance_counter;
  options.performance_frequency = clock->performance_frequency;
  options.next_frame = next_frame; options.next_frame_ctx = frame_ctx;
  char *map_name = qa_ss_map_name(sim->recipe.map.geometry.requested_path);
  qa_app_sim_native_q2_map_geometry geometry = {sim->world_kind, sim->world_entities,
      (double)(sim->world.kind == QA_DECODED_WORLD_Q1 ? sim->world.as.q1->model_count
               : sim->world.kind == QA_DECODED_WORLD_Q2 ? sim->world.as.q2->model_count : sim->world.as.q3->model_count)};
  char *entities = qa_app_sim_prepare_native_q2_map(geometry, "rerelease", sim->options.mode);
  if (map_name == NULL || entities == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, map_name); ss_own(sim, ss_dispose_free, entities);
  options.map = (qa_app_sim_classic_guest_map){map_name, entities, ""};
  options.ownership.inventory = "source-private"; options.ownership.presentation = "selected-weapon";
  qa_app_sim_classic_guest_services_options *base = &options.services.base;
  base->engine = &record->engine; base->scene = sim->scene; base->cvars = cvars; base->numeric = *qa_create_numeric_operations(&timing.numeric);
  base->map_path = sim->recipe.map.geometry.requested_path; base->max_clients = sim->options.max_clients;
  base->admit = (qa_app_sim_guest_admit){record, ss_native_admit};
  base->collision = (qa_app_sim_guest_collision){record, ss_native_collision};
  base->command = (qa_app_sim_guest_command){record, ss_native_command};
  base->add_command = (qa_app_sim_guest_add_command){record, ss_native_add_command};
  base->debug_graph = (qa_app_sim_guest_debug_graph){record, ss_native_debug_graph};
  base->print = (qa_app_sim_guest_print){record, ss_native_behavior_print};
  options.services.frame_milliseconds = 25;
  options.services.localize = (qa_app_sim_rerelease_localize){record, ss_native_localize};
  options.services.clipboard = (qa_app_sim_rerelease_clipboard){false, NULL, NULL}; /* { kind: "dedicated" } */
  options.services.debug_shapes = (qa_app_sim_rerelease_debug_shapes){record, ss_native_debug_shapes};
  options.services.world_text = (qa_app_sim_rerelease_world_text){record, ss_native_world_text};
  options.services.navigation = qa_app_shared_sim_rerelease_navigation_services(sim);
  options.ctx = record; options.actor = ss_native_weapon_actor;
  qa_app_sim_rerelease_weapon_behavior_source *source = qa_app_sim_rerelease_weapon_behavior_source_create(&options);
  if (source == NULL) qa_range_error("Out of memory");
  return source;
}

/* ---- C-only release of the B2 owners ---- */
void qa_app_shared_sim_arsenal_destroy(qa_app_shared_simulation *sim) {
  if (sim->hand_grenades != NULL) { qa_app_sim_hand_grenade_runtime_destroy(sim->hand_grenades); sim->hand_grenades = NULL; }
  if (sim->grapple != NULL) {
    qa_app_sim_grapple_source source = *qa_app_sim_grapple_runtime_source(sim->grapple);
    qa_app_sim_grapple_runtime_destroy(sim->grapple); sim->grapple = NULL;
    if (qa_ss_is(source.kind, "q3-qvm")) qa_app_sim_qvm_grapple_source_destroy(source.q3_game);
  }
  if (sim->has_selected_arsenal) {
    if (sim->selected_arsenal.q1) qa_app_q1_selected_arsenal_destroy(sim->selected_arsenal.q1);
    if (sim->selected_arsenal.q2) qa_app_q2_selected_arsenal_destroy(sim->selected_arsenal.q2);
    if (sim->selected_arsenal.q3) qa_app_q3_selected_arsenal_destroy(sim->selected_arsenal.q3);
    memset(&sim->selected_arsenal, 0, sizeof sim->selected_arsenal); sim->has_selected_arsenal = false;
  }
  if (sim->selected_q3_source != NULL) { qa_app_q3_selected_source_destroy(sim->selected_q3_source); sim->selected_q3_source = NULL; }
  if (sim->selected_ammo_timers != NULL) { qa_app_q3_mapped_ammo_regeneration_destroy(sim->selected_ammo_timers); sim->selected_ammo_timers = NULL; }
  /* selectedQ3Strings values are malloc'd copies the configstring store wrote. */
  for (size_t i = 0; i < sim->selected_q3_strings.count; i++) free(*(char **)qa_ss_map_value_at(&sim->selected_q3_strings, i));
  if (sim->selected_q3_strings.value_size != 0) qa_ss_map_clear(&sim->selected_q3_strings);
  while (sim->b2_owned_count > 0) {
    qa_ss_dispose owner = sim->b2_owned[--sim->b2_owned_count];
    owner.dispose(owner.ctx);
  }
  free(sim->b2_owned); sim->b2_owned = NULL; sim->b2_owned_capacity = 0;
  for (size_t i = 0; i < sim->b2_player_ammo.count; i++) free(*(qa_inventory_entry **)qa_ss_map_value_at(&sim->b2_player_ammo, i));
  if (sim->b2_player_ammo.value_size != 0) qa_ss_map_free(&sim->b2_player_ammo);
}

/* ---- prepareSelectedOriginalPickups (2335-2430) ---- */
typedef struct ss_pickup_rule {
  qa_app_shared_simulation *sim; qa_ss_source_kind kind; qa_shared_pickup_admission *admission;
  qa_app_sim_quakec_source *quakec; qa_qvm_primary_pickups *qvm_pickups; bool qvm_has_pickups;
  qa_compat_q2_native_pickup_supply_evaluation original; /* q2-native: the evaluation the resolver reads */
  qa_qvm_pickup_supply qvm_supply;
} ss_pickup_rule;
static bool ss_rule_current(qa_original_pickup_execution execution) { return execution.current(execution.current_ctx); }
/* entry => quantity(entry.count, entry.capacity) */
static qa_pickup_quantity_result ss_native_quantity(void *ctx, qa_inventory_entry entry) {
  ss_pickup_rule *rule = ctx;
  qa_source_pickup_quantity quantity = rule->original.quantity(rule->original.ctx, entry.count, entry.capacity);
  qa_pickup_quantity_result result = {true, quantity.amount, quantity.accepted};
  return result;
}
static qa_pickup_quantity_result ss_qvm_quantity(void *ctx, qa_inventory_entry entry) {
  qa_pickup_quantity_result result = {false, qa_qvm_pickup_supply_quantity(&((ss_pickup_rule *)ctx)->qvm_supply, entry.count), false};
  return result;
}
static qa_original_pickup_decision ss_decision(bool accepted) { return accepted ? QA_ORIGINAL_PICKUP_ACCEPTED : QA_ORIGINAL_PICKUP_REFUSED; }
static qa_original_pickup_decision ss_native_take(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_execution execution) {
  ss_pickup_rule *rule = ctx; qa_app_shared_simulation *sim = rule->sim;
  qa_owned_actor actor;
  if (!ss_owned(sim, offer.recipient, &actor) || !ss_rule_current(execution)) return QA_ORIGINAL_PICKUP_REFUSED;
  rule->original = ss_native_classic(sim)
      ? qa_compat_q2_classic_guest_host_pickup_supply(qa_app_sim_classic_guest_source_host(qa_app_sim_classic_guest_world_source(sim->source.classic_world)), &offer)
      : qa_compat_q2_rerelease_q2_guest_host_pickup_supply(qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_world_source(sim->source.rerelease_world)), &offer);
  qa_pickup_quantity_fn resolve = rule->original.quantity != NULL ? ss_native_quantity : NULL;
  const qa_pickup_supply_offer *original = &rule->original.offer;
  qa_app_sim_dropped_pickup_cargo cargo;
  if (qa_app_sim_source_pickup_cargo_get(sim->dropped_pickups, offer.pickup, &cargo)) {
    const qa_selected_pickup_weapon *weapon = NULL;
    size_t count = 0;
    const qa_selected_pickup_weapon *catalog = sim->has_selected_arsenal ? ss_sel_catalog(sim, &count) : NULL;
    for (size_t i = 0; i < count && weapon == NULL; i++) if (qa_ss_is(catalog[i].item, cargo.item)) weapon = &catalog[i];
    qa_pickup_supply_offer exact; memset(&exact, 0, sizeof exact);
    qa_pickup_ammo_grant grants[16];
    if (original->kind == QA_PICKUP_SUPPLY_WEAPON) {
      if (weapon == NULL) ss_fail("Dropped weapon has no selected inventory definition");
      exact.kind = QA_PICKUP_SUPPLY_WEAPON; exact.item = cargo.item;
      if (weapon->ammo != NULL) {
        if (original->ammo_count > 16) ss_fail("Dropped weapon grants exceed their storage");
        for (size_t i = 0; i < original->ammo_count; i++) { grants[i] = original->ammo[i]; grants[i].item = weapon->ammo; }
        exact.ammo = grants; exact.ammo_count = original->ammo_count;
      }
    } else if (original->kind == QA_PICKUP_SUPPLY_AMMO_WEAPON && weapon != NULL) {
      exact.kind = QA_PICKUP_SUPPLY_AMMO_WEAPON; exact.grant.item = weapon->ammo != NULL ? weapon->ammo : cargo.item;
      exact.weapon = cargo.item; exact.grant.amount = original->grant.amount;
    } else { exact.kind = QA_PICKUP_SUPPLY_AMMO; exact.grant.item = cargo.item; exact.grant.amount = original->grant.amount; }
    return ss_decision(qa_shared_pickup_admission_canonical(rule->admission, actor, exact,
        exact.kind == QA_PICKUP_SUPPLY_AMMO_WEAPON ? QA_PICKUP_SELECTION_ALWAYS : QA_PICKUP_SELECTION_BETTER, resolve, rule));
  }
  if (original->kind == QA_PICKUP_SUPPLY_AMMO) return ss_decision(qa_shared_pickup_admission_ammo(rule->admission, actor, original->grant, true, resolve, rule));
  if (original->kind == QA_PICKUP_SUPPLY_AMMO_WEAPON) {
    qa_pickup_ammo_weapon_offer ammo_weapon = {original->grant, original->weapon};
    qa_ammo_weapon_selection selection = {QA_PICKUP_SELECTION_ALWAYS, "empty-ammo"};
    return ss_decision(qa_shared_pickup_admission_ammo_weapon(rule->admission, actor, ammo_weapon, selection, resolve, rule));
  }
  qa_pickup_weapon_offer weapon_offer = {original->item, original->ammo, original->ammo_count};
  return ss_decision(qa_shared_pickup_admission_weapon(rule->admission, actor, weapon_offer, QA_PICKUP_SELECTION_BETTER, resolve, rule));
}
static qa_original_pickup_decision ss_qvm_take(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_execution execution) {
  ss_pickup_rule *rule = ctx; qa_app_shared_simulation *sim = rule->sim;
  qa_owned_actor actor;
  if (!ss_owned(sim, offer.recipient, &actor) || !ss_rule_current(execution) || sim->source.qvm_pickups == NULL) return QA_ORIGINAL_PICKUP_REFUSED;
  qa_qvm_primary_pickups_supply(&rule->qvm_supply, sim->source.qvm_pickups, offer);
  const qa_pickup_supply_offer *original = &rule->qvm_supply.offer;
  if (original->kind == QA_PICKUP_SUPPLY_AMMO) return ss_decision(qa_shared_pickup_admission_ammo(rule->admission, actor, original->grant, true, NULL, NULL));
  if (original->kind != QA_PICKUP_SUPPLY_WEAPON) ss_fail("Original QVM pickup has no static supply grant");
  qa_pickup_weapon_offer weapon_offer = {original->item, original->ammo, original->ammo_count};
  return ss_decision(qa_shared_pickup_admission_weapon(rule->admission, actor, weapon_offer, QA_PICKUP_SELECTION_BETTER,
      rule->qvm_supply.has_quantity ? ss_qvm_quantity : NULL, rule));
}
static qa_original_pickup_decision ss_qc_take(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_execution execution) {
  ss_pickup_rule *rule = ctx; qa_app_shared_simulation *sim = rule->sim;
  if (!ss_rule_current(execution)) return QA_ORIGINAL_PICKUP_REFUSED;
  qa_owned_actor actor;
  if (!ss_owned(sim, offer.recipient, &actor)) return QA_ORIGINAL_PICKUP_REFUSED;
  if (offer.cargo != NULL)
    return ss_decision(qa_shared_pickup_admission_cargo(rule->admission, actor, offer.cargo, offer.cargo_count,
        qa_app_sim_quakec_source_pickup_selection_deferred(rule->quakec, offer) ? QA_PICKUP_SELECTION_NEVER : QA_PICKUP_SELECTION_BETTER));
  qa_q1_id1_pickup_supply original = qa_app_sim_quakec_source_pickup_supply(rule->quakec, offer);
  if (original.offer.kind == QA_PICKUP_SUPPLY_AMMO) return ss_decision(qa_shared_pickup_admission_ammo(rule->admission, actor, original.offer.grant, true, NULL, NULL));
  if (original.offer.kind != QA_PICKUP_SUPPLY_WEAPON) ss_fail("Original QC pickup has no static supply grant");
  if (original.leave && qa_shared_pickup_admission_owns(rule->admission, actor.id, original.offer.item)) return QA_ORIGINAL_PICKUP_REFUSED;
  if (original.offer.ammo_count != 0) original.offer.ammo = &original.ammo_storage; /* the copy's grant storage */
  qa_pickup_weapon_offer weapon_offer = {original.offer.item, original.offer.ammo, original.offer.ammo_count};
  return ss_decision(qa_shared_pickup_admission_weapon(rule->admission, actor, weapon_offer,
      qa_app_sim_quakec_source_pickup_selection_deferred(rule->quakec, offer) ? QA_PICKUP_SELECTION_NEVER : QA_PICKUP_SELECTION_BETTER, NULL, NULL));
}
/* selectedPickupPolicy (quakec) */
typedef struct ss_pickup_policy { qa_app_shared_simulation *sim; const qa_pickup_supply_profile *profile; qa_shared_pickup_admission *admission; qa_q1qc_pickup_policy policy; } ss_pickup_policy;
static qa_item_id ss_policy_current(void *ctx, qa_owned_actor actor) {
  ss_pickup_policy *policy = ctx; qa_app_shared_simulation *sim = policy->sim;
  qa_item_id current = NULL;
  if (sim->has_selected_arsenal) { qa_arsenal_state state = ss_sel_read(sim, actor.id); current = state.active_weapon; ss_arsenal_free(&state); }
  for (size_t i = 0; i < policy->profile->weapon_count; i++)
    for (size_t j = 0; j < policy->profile->weapons[i].destination_count; j++)
      if (current != NULL && qa_ss_is(policy->profile->weapons[i].destinations[j], current)) return policy->profile->weapons[i].source;
  return NULL;
}
static void ss_policy_select(void *ctx, qa_owned_actor actor, qa_item_id item) {
  qa_shared_pickup_admission_select_weapon(((ss_pickup_policy *)ctx)->admission, actor, item, QA_PICKUP_SELECTION_BETTER);
}
static void ss_policy_counter(void *ctx, qa_owned_actor actor, qa_item_id item, qa_q1qc_pickup_counter_original_fn original, void *original_ctx) {
  ss_pickup_policy *policy = ctx; qa_app_shared_simulation *sim = policy->sim;
  const qa_pickup_supply_mapping *mapping = NULL;
  for (size_t i = 0; i < policy->profile->ammo_count && mapping == NULL; i++) if (qa_ss_is(policy->profile->ammo[i].source, item)) mapping = &policy->profile->ammo[i];
  if (mapping == NULL) ss_fail("Original pickup counter has no selected supply mapping");
  for (size_t i = 0; i < mapping->destination_count; i++)
    if (ss_entry(sim, actor.id, mapping->destinations[i]) == NULL) ss_fail("Original pickup counter destination was not admitted");
  for (size_t i = 0; i < mapping->destination_count; i++) {
    qa_session_actor_registry_assert_owned(sim->actors, actor);
    const qa_inventory_entry *found = ss_entry(sim, actor.id, mapping->destinations[i]);
    if (found == NULL) ss_fail("Original pickup counter destination retired");
    qa_inventory_entry current = *found;
    double value;
    bool has_value = original(original_ctx, current.count, &value);
    qa_session_actor_registry_assert_owned(sim->actors, actor);
    if (has_value) { current.count = value; ss_configure(sim, actor, current); }
  }
}
/* One rule over mapping: writes are the unique destinations (counts), id "selected:<source>". */
static void ss_original_rule(qa_app_shared_simulation *sim, const qa_pickup_supply_profile *profile, qa_item_id source,
    const qa_item_id *destinations, size_t destination_count, bool ammo, ss_pickup_rule *ctx,
    qa_original_pickup_decision (*take)(void *, qa_original_pickup_offer, qa_original_pickup_execution), qa_original_pickup_rule *out) {
  size_t total = destination_count;
  if (!ammo) for (size_t i = 0; i < profile->ammo_count; i++) total += profile->ammo[i].destination_count;
  qa_pickup_write *writes = qa_app_shared_sim_keep(sim, (total + 1) * sizeof *writes);
  size_t count = 0;
  for (size_t i = 0; i < total; i++) {
    qa_item_id item;
    if (i < destination_count) item = destinations[i];
    else {
      size_t k = i - destination_count, j = 0;
      while (k >= profile->ammo[j].destination_count) k -= profile->ammo[j++].destination_count;
      item = profile->ammo[j].destinations[k];
    }
    bool seen = false;
    for (size_t j = 0; j < count && !seen; j++) seen = qa_ss_is(writes[j].item, item);
    if (seen) continue;
    writes[count].kind = QA_PICKUP_RESOURCE_INVENTORY; writes[count].item = item; writes[count].fields = "count";
    count++;
  }
  if (count == 0) ss_fail((sim->strategy->as_q2_native(sim) != NULL) ? "Selected native pickup has no destination" : "Selected original pickup has no destination");
  char id[256]; snprintf(id, sizeof id, "selected:%s", source);
  qa_item_id *offered = qa_app_shared_sim_keep(sim, sizeof *offered); *offered = source;
  memset(out, 0, sizeof *out);
  out->ctx = ctx; out->id = qa_app_shared_sim_intern(sim, id); out->offered = offered; out->offered_count = 1;
  out->writes = (qa_pickup_writes){writes, count}; out->take = take;
}
static bool ss_mapping_is_ammo(const qa_pickup_supply_profile *profile, const qa_pickup_supply_mapping *mapping) {
  for (size_t i = 0; i < profile->ammo_count; i++) if (&profile->ammo[i] == mapping) return true;
  return false;
}
void qa_app_shared_sim_prepare_selected_original_pickups(qa_app_shared_simulation *sim, const qa_pickup_supply_profile *profile) {
  sim->selected_original_supply = profile;
  bool native = ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE), qvm = ss_source_is(sim, QA_SS_SOURCE_Q3_QVM), quakec = ss_source_is(sim, QA_SS_SOURCE_QUAKEC);
  if (sim->selected_supply == NULL || !(native || qvm || quakec)) return;
  size_t mapping_count = profile->ammo_count + profile->weapon_count;
  const qa_pickup_supply_mapping **mappings = qa_app_shared_sim_keep(sim, (mapping_count + 1) * sizeof *mappings);
  size_t count = 0;
  for (size_t i = 0; i < mapping_count; i++) {
    const qa_pickup_supply_mapping *mapping = i < profile->ammo_count ? &profile->ammo[i] : &profile->weapons[i - profile->ammo_count];
    size_t at = count;
    /* q2-native: new Map(mappings by source): a later mapping replaces an earlier one in place. */
    if (native) for (size_t j = 0; j < count; j++) if (qa_ss_is(mappings[j]->source, mapping->source)) { at = j; break; }
    mappings[at] = mapping;
    if (at == count) count++;
  }
  size_t rule_count = count + (quakec ? 1 : 0);
  qa_original_pickup_rule *rules = qa_app_shared_sim_keep(sim, (rule_count + 1) * sizeof *rules);
  ss_pickup_rule *contexts = qa_app_shared_sim_keep(sim, (rule_count + 1) * sizeof *contexts);
  for (size_t i = 0; i < rule_count; i++) {
    ss_pickup_rule *rule = &contexts[i];
    rule->sim = sim; rule->kind = sim->source.kind; rule->admission = sim->selected_supply;
    rule->quakec = quakec ? sim->source.quakec : NULL;
    qa_original_pickup_decision (*take)(void *, qa_original_pickup_offer, qa_original_pickup_execution) = native ? ss_native_take : qvm ? ss_qvm_take : ss_qc_take;
    if (i < count) ss_original_rule(sim, profile, mappings[i]->source, mappings[i]->destinations, mappings[i]->destination_count,
        ss_mapping_is_ammo(profile, mappings[i]), rule, take, &rules[i]);
    else {
      /* { source: "q1:item_backpack", destinations: every weapon destination } */
      size_t total = 0;
      for (size_t j = 0; j < profile->weapon_count; j++) total += profile->weapons[j].destination_count;
      qa_item_id *destinations = qa_app_shared_sim_keep(sim, (total + 1) * sizeof *destinations);
      size_t k = 0;
      for (size_t j = 0; j < profile->weapon_count; j++)
        for (size_t d = 0; d < profile->weapons[j].destination_count; d++) destinations[k++] = profile->weapons[j].destinations[d];
      ss_original_rule(sim, profile, "q1:item_backpack", destinations, total, false, rule, take, &rules[i]);
    }
  }
  if (quakec) {
    ss_pickup_policy *policy = qa_app_shared_sim_keep(sim, sizeof *policy);
    policy->sim = sim; policy->profile = profile; policy->admission = sim->selected_supply;
    policy->policy = (qa_q1qc_pickup_policy){policy, ss_policy_current, ss_policy_select, ss_policy_counter};
    sim->selected_pickup_policy = &policy->policy;
  }
  sim->selected_original_pickups = rules; sim->selected_original_pickup_count = rule_count;
}

/* ---- createSelectedQ1Arsenal (2651-2743) ---- */
/* { ...host, powerupExpires, sourceDamageModifier?, sourceDamagePowerupOwner?, weaponVolume, weaponImpact,
 * sourceDamageMultiplier }: the q1ActorHost members run with their own ctx. */
typedef struct ss_q1_selected {
  qa_app_shared_simulation *sim; qa_q1_host base; qa_source_damage_modifier modifier;
  qa_q1_services *game; qa_comp_selected_q1_mission_weapons *mission; qa_q1_weapon_rules cadence;
  qa_composition_owned_profile expanded, profile; qa_pickup_supply_profile view;
  qa_pickup_supply_mapping *identity_ammo, *identity_weapons; qa_item_id *replaced;
  qa_pickup_admission admission; qa_app_q1_selected_arsenal *arsenal;
} ss_q1_selected;
static double fw_random(void *c) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->random(b->ctx); }
static qa_q1_trace fw_trace(void *c, qa_q1_trace_request r) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->trace(b->ctx, r); }
static qa_q1_contents fw_contents(void *c, qa_vec3 p) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->contents(b->ctx, p); }
static bool fw_walk_move(void *c, qa_owned_actor a, double yaw, double d) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->walk_move(b->ctx, a, yaw, d); }
static void fw_change_yaw(void *c, qa_owned_actor a) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->change_yaw(b->ctx, a); }
static void fw_move_to_goal(void *c, qa_owned_actor a, qa_actor_id g, double d, const char *m) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->move_to_goal(b->ctx, a, g, d, m); }
static bool fw_check_bottom(void *c, qa_actor_id a) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->check_bottom(b->ctx, a); }
static void fw_schedule_think(void *c, qa_owned_actor a, double due) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->schedule_think(b->ctx, a, due); }
static void fw_cancel_think(void *c, qa_owned_actor a) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->cancel_think(b->ctx, a); }
static void fw_emit(void *c, qa_q1_event e) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->emit(b->ctx, e); }
static void fw_transition(void *c, qa_transition_intent i) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->transition(b->ctx, i); }
static const qa_actor_id *fw_players(void *c, size_t *n) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->players(b->ctx, n); }
static bool fw_check_client(void *c, qa_owned_actor o, qa_actor_id *out) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->check_client(b->ctx, o, out); }
static const char *fw_classname(void *c, qa_actor_id a) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->classname(b->ctx, a); }
static void fw_powerup(void *c, qa_owned_actor a, qa_q1_powerup p, double e) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->powerup(b->ctx, a, p, e); }
static qa_q1_monster_target_result fw_monster_target(void *c, qa_actor_id a) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->monster_target(b->ctx, a); }
static void fw_set_gravity(void *c, qa_actor_id a, double s) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->set_gravity(b->ctx, a, s); }
static void fw_control_player(void *c, qa_actor_id a, qa_vec3 o, qa_vec3 g, qa_vec3 v) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->control_player(b->ctx, a, o, g, v); }
static bool fw_source_target(void *c, qa_actor_id a, qa_q1_source_target *out) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->source_target(b->ctx, a, out); }
static qa_q1_pusher_services fw_pusher_services(void *c, qa_q1_services *g) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->pusher_services(b->ctx, g); }
static qa_vec3 fw_punch_read(void *c, qa_actor_id a) { qa_q1_host *b = &((ss_q1_selected *)c)->base; return b->punch_read(b->ctx, a); }
static void fw_punch_write(void *c, qa_actor_id a, qa_vec3 v) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->punch_write(b->ctx, a, v); }
static void fw_register_entity(void *c, qa_q1_actor *e, qa_q1_services *s) { qa_q1_host *b = &((ss_q1_selected *)c)->base; b->register_entity(b->ctx, e, s); }
/* powerupExpires(actor, powerup) */
static double ss_q1_powerup_expires(void *ctx, qa_actor_id actor, qa_q1_powerup powerup) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  if (!qa_session_actor_registry_is_live(sim->actors, actor)) return 0;
  return sim->strategy->q1_powerup_expires(sim, actor, powerup);
}
/* TS host.selectedQ1Powerup(actor, powerup): the selected Q1 weapon source's player powerup. */
double qa_app_shared_sim_selected_q1_powerup(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  if (!sim->has_selected_weapon_source || !qa_ss_is(sim->selected_weapon_source.kind, "q1")) return 0;
  qa_q1_player_state *player = qa_q1_player(sim->selected_weapon_source.q1_game, actor);
  return player != NULL ? qa_q1_player_powerup_get(player, powerup) : 0;
}
/* Q3Strategy.q1PowerupExpires */
double qa_ss_q3_q1_powerup_expires(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  qa_q3_game_client *client = ss_q3_native_client(sim, actor);
  double key = qa_ss_is(powerup, "quad") ? QA_Q3_PW_QUAD : qa_ss_is(powerup, "invisibility") ? QA_Q3_PW_INVIS : qa_ss_is(powerup, "suit") ? QA_Q3_PW_BATTLESUIT : -1;
  if (qa_ss_is(powerup, "invulnerability")) return (client != NULL ? client->invulnerability_time : 0) / 1000;
  return key < 0 ? 0 : (client != NULL ? ss_q3_powerup(client, key) : 0) / 1000;
}
static double ss_q1_weapon_volume(void *ctx, qa_actor_id actor) {
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  bool has = qa_app_shared_sim_q2_item_weapon_source(((ss_q1_selected *)ctx)->sim, &game, &weapons);
  return (has ? qa_q2w_ballistics_silencer_shots(&weapons->base, actor) : 0) > 0 ? 0.2 : 1;
}
static void ss_q1_weapon_impact(void *ctx, qa_actor_id actor, qa_vec3 origin) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  sim->strategy->weapon_noise(sim, actor, origin, "impact");
}
static double ss_q1_damage_multiplier(void *ctx, qa_actor_id attacker) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  double factor;
  if (sim->strategy->weapon_damage_factor(sim, attacker, true, &factor)) return factor;
  return qa_app_sim_provider_family(sim->recipe.combat.provider) == QA_GAME_FAMILY_Q1
      || ss_q1_powerup_expires(ctx, attacker, "quad") <= qa_ss_seconds(qa_app_shared_sim_selected_q1_frame(sim).time) ? 1 : 4;
}
/* registerSelectedQ1MissionWeapons services */
static void ss_q1_mission_emit(void *ctx, const qa_q1ad_event *event) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  qa_app_shared_sim_emit_q1_addon(sim, sim->weapon_provider.content, event, qa_app_shared_sim_selected_q1_frame(sim).time);
}
static bool ss_q1_is_monster(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  const qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (entry == NULL) return false;
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
    qa_app_sim_quakec_weapon_target target;
    return qa_app_sim_quakec_source_weapon_target(sim->source.quakec, actor, &target) && target.monster;
  }
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) return (qa_js_to_int32(entry->q1_entity->movement_flags) & 32) != 0;
  return entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && (qa_js_to_int32(entry->q2_entity->server_flags) & 4) != 0;
}
/* this.strategy.cvars() ?? this.q2ServerRegistry */
static qa_cvar_registry *ss_q1_cvar_registry(qa_app_shared_simulation *sim) {
  qa_cvar_registry *own = sim->strategy->cvars(sim);
  return own != NULL ? own : sim->q2_server_registry;
}
static double ss_q1_mission_cvar(void *ctx, const char *name) {
  qa_cvar_registry *registry = ss_q1_cvar_registry(((ss_q1_selected *)ctx)->sim);
  return registry != NULL ? qa_cvar_registry_variable_value(registry, name) : 0;
}
static void ss_q1_mission_set_cvar(void *ctx, const char *name, const char *value) {
  qa_cvar_registry *registry = ss_q1_cvar_registry(((ss_q1_selected *)ctx)->sim);
  if (registry == NULL) ss_fail("Selected Q1 weapons require the shared server settings registry");
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(registry, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}
/* composition:q3-cadence */
static double ss_q1_cadence(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay) {
  (void)game; return qa_app_shared_sim_selected_weapon_delay(((ss_q1_selected *)ctx)->sim, player->actor.id, delay);
}
/* Q1SelectedArsenal options */
static bool ss_q1_impulse(void *ctx, qa_q1_player_state *player, double value) {
  return qa_comp_selected_q1_mission_weapons_impulse(((ss_q1_selected *)ctx)->mission, player, value);
}
static void ss_q1_prepare_pickup(void *ctx, qa_q1_player_state *player) {
  qa_comp_selected_q1_mission_weapons_prepare_pickup(((ss_q1_selected *)ctx)->mission, player);
}
static qa_app_q1_native_player ss_q1_native_player(void *ctx, qa_actor_id actor) {
  ss_q1_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_app_q1_native_player native; memset(&native, 0, sizeof native);
  if (ss_source_is(sim, QA_SS_SOURCE_Q1)) {
    qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
    if (player == NULL) ss_fail("Selected Q1 arsenal has no native map player");
    native.has_value = true; native.weapon = player->weapon; native.max_health = player->max_health;
    native.has_auto_switch = true; native.auto_switch = player->auto_switch;
    return native;
  }
  if (!ss_source_is(sim, QA_SS_SOURCE_QUAKEC)) ss_fail("Selected Q1 arsenal lost its original QC owner");
  qa_arsenal_state arsenal = qa_app_sim_quakec_source_client_arsenal(sim->source.quakec, actor);
  qa_q1_services *game = record->game;
  qa_q1_weapon weapon = NULL;
  for (size_t i = 0; i < 8 + game->registered_weapon_count && weapon == NULL; i++) {
    qa_q1_weapon candidate = i < 8 ? QA_Q1_WEAPONS[i] : game->registered_weapons[i - 8].def->id;
    if (qa_ss_is(qa_q1_services_weapon_item(game, candidate), arsenal.active_weapon)) weapon = candidate;
  }
  if (weapon == NULL) ss_fail("Original QC weapon has no selected Q1 definition");
  native.has_value = true; native.weapon = weapon; native.max_health = qa_app_sim_quakec_source_client_equipment(sim->source.quakec, actor).max_health;
  return native;
}
static qa_actor_animation_state ss_q1_fired(void *ctx, qa_actor_id actor, qa_q1_weapon weapon, qa_actor_animation_state animation) {
  ss_q1_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_app_shared_sim_weapon_character_animation(sim, actor, "attack", qa_ss_is(weapon, "axe") || qa_ss_is(weapon, "hipnotic:mjolnir"),
      sim->weapon_provider.content, false);
  sim->strategy->note_fired(sim, actor, qa_q1_services_weapon_item(record->game, weapon));
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  qa_body_state body;
  if (qa_app_shared_sim_body(sim, actor, &body)) sim->strategy->weapon_noise(sim, actor, body.origin, "weapon");
  return player != NULL ? player->animation : animation;
}
static void ss_q1_observe(void *ctx, qa_actor_id actor, qa_vec3 *out_view_angles, double *out_water_level) {
  qa_ss_arsenal_observation player;
  if (!qa_app_shared_sim_arsenal_observation(((ss_q1_selected *)ctx)->sim, actor, &player)) ss_fail("Selected Q1 arsenal lost its player");
  *out_view_angles = player.view_angles; *out_water_level = player.water_level;
}
/* SharedPickupAdmission grants */
static void ss_q1_ammo_granted(void *ctx, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  qa_app_q1_selected_arsenal_pickup_ammo(((ss_q1_selected *)ctx)->arsenal, actor, grants, count, auto_switch);
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor.id);
}
static void ss_q1_weapon_granted(void *ctx, qa_owned_actor actor, const qa_item_id *weapons, size_t count, qa_pickup_selection selection) {
  qa_app_shared_simulation *sim = ((ss_q1_selected *)ctx)->sim;
  qa_app_q1_selected_arsenal_pickup_weapons(((ss_q1_selected *)ctx)->arsenal, actor, weapons, count, selection);
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor.id);
}
static void ss_dispose_mission_q1(void *ctx) { qa_comp_selected_q1_mission_weapons_destroy(ctx); }
void qa_app_shared_sim_create_selected_q1_arsenal(qa_app_shared_simulation *sim, qa_ss_selected_arsenal *out) {
  char product[32] = "";
  { const char *content = sim->weapon_provider.content, *first = strchr(content, ':'), *second = first ? strchr(first + 1, ':') : NULL;
    if (second != NULL) { const char *end = strchr(second + 1, ':'); size_t length = end ? (size_t)(end - second - 1) : strlen(second + 1);
      if (length < sizeof product) { memcpy(product, second + 1, length); product[length] = '\0'; } } }
  static const char *const programs[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3"};
  int program = -1;
  for (int i = 0; i < 6; i++) if (strcmp(product, programs[i]) == 0) program = i;
  if (program < 0) ss_fail("Selected Q1 arsenal has an unsupported source program");
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider);
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, "classic");
  if (random == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_random, random);
  ss_q1_selected *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim;
  record->base = qa_app_shared_sim_q1_actor_host(sim, sim->weapon_provider,
      ss_runtime(sim, timing.numeric, random, SS_CLOCK_SELECTED_Q1, NULL, 0), NULL);
  const qa_q1_host *b = &record->base;
  qa_q1_host host = *b;
  host.ctx = record;
  host.random = b->random ? fw_random : NULL; host.trace = b->trace ? fw_trace : NULL; host.contents = b->contents ? fw_contents : NULL;
  host.walk_move = b->walk_move ? fw_walk_move : NULL; host.change_yaw = b->change_yaw ? fw_change_yaw : NULL;
  host.move_to_goal = b->move_to_goal ? fw_move_to_goal : NULL; host.check_bottom = b->check_bottom ? fw_check_bottom : NULL;
  host.schedule_think = b->schedule_think ? fw_schedule_think : NULL; host.cancel_think = b->cancel_think ? fw_cancel_think : NULL;
  host.emit = b->emit ? fw_emit : NULL; host.transition = b->transition ? fw_transition : NULL; host.players = b->players ? fw_players : NULL;
  host.check_client = b->check_client ? fw_check_client : NULL; host.classname = b->classname ? fw_classname : NULL;
  host.powerup = b->powerup ? fw_powerup : NULL; host.monster_target = b->monster_target ? fw_monster_target : NULL;
  host.set_gravity = b->set_gravity ? fw_set_gravity : NULL; host.control_player = b->control_player ? fw_control_player : NULL;
  host.source_target = b->source_target ? fw_source_target : NULL; host.pusher_services = b->pusher_services ? fw_pusher_services : NULL;
  host.punch_read = b->punch_read ? fw_punch_read : NULL; host.punch_write = b->punch_write ? fw_punch_write : NULL;
  host.register_entity = b->register_entity ? fw_register_entity : NULL;
  host.powerup_expires = ss_q1_powerup_expires;
  host.source_damage_modifier = qa_app_shared_sim_damage_modifier_option(sim, &record->modifier);
  if (sim->strategy->runs_q3_powerups(sim)) host.source_damage_powerup_owner = sim->recipe.map.entities.provider;
  host.weapon_volume = ss_q1_weapon_volume; host.weapon_impact = ss_q1_weapon_impact; host.source_damage_multiplier = ss_q1_damage_multiplier;
  qa_q1_options options; memset(&options, 0, sizeof options);
  options.provider = sim->weapon_provider.provider; options.edition = strstr(sim->weapon_provider.content, ":rerelease:") ? "rerelease" : "classic";
  options.physics_edition = qa_app_shared_sim_q1_physics_edition(sim); options.skill = sim->options.skill;
  options.deathmatch = qa_ss_is(sim->options.mode, "deathmatch") ? 1 : 0; options.coop = qa_ss_is(sim->options.mode, "coop");
  options.campaign = sim->recipe.map.entities.provider; options.combat_provider = sim->recipe.combat.provider;
  options.movement_provider = sim->recipe.movement.provider; options.inventory_provider = sim->recipe.inventory.provider;
  options.gravity = qa_app_sim_shared_physics_gravity(sim->physics);
  qa_q1_services *game = calloc(1, sizeof *game);
  if (game == NULL || !qa_q1_services_init(game, host, options)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, game); ss_own(sim, ss_dispose_q1_game, game);
  record->game = game;
  if (program == QA_COMP_SELECTED_Q1_MG3 && sim->options.restore == NULL) game->world = qa_q1_create(game, "worldspawn", NULL, NULL);
  record->mission = calloc(1, sizeof *record->mission);
  if (record->mission == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->mission);
  qa_q1ad_services services = {record, NULL, ss_q1_mission_emit, ss_q1_is_monster, ss_q1_mission_cvar, ss_q1_mission_set_cvar};
  qa_comp_register_selected_q1_mission_weapons(record->mission, game, (qa_comp_selected_q1_program)program, services);
  ss_own(sim, ss_dispose_mission_q1, record->mission);
  if (sim->strategy->runs_q3_powerups(sim)) {
    memset(&record->cadence, 0, sizeof record->cadence);
    record->cadence.ctx = record; record->cadence.id = "composition:q3-cadence";
    record->cadence.attack_delay = ss_q1_cadence; record->cadence.frame_delay = ss_q1_cadence;
    qa_q1_register_weapon_rules(game, &record->cadence);
  }
  if (qa_app_shared_sim_foreign_lmctf(sim)) qa_app_shared_sim_register_lmctf_q1_haste_rules(sim, game);
  if (program == QA_COMP_SELECTED_Q1_MG3) qa_ss_map_set(&sim->q1_character_adjuncts, &game, NULL);
  memset(&sim->selected_weapon_source, 0, sizeof sim->selected_weapon_source);
  sim->has_selected_weapon_source = true;
  sim->selected_weapon_source.kind = "q1"; sim->selected_weapon_source.q1_game = game; sim->selected_weapon_source.random = random;
  sim->selected_weapon_source.mission_weapons = record->mission;
  /* baseProfile */
  const char *q1_family = sim->strategy->supply_family(sim);
  bool q1_source = qa_ss_is(q1_family, "q1"), q2_source = qa_ss_is(q1_family, "q2");
  qa_pickup_supply_profile base;
  if (program == QA_COMP_SELECTED_Q1_HIPNOTIC)
    base = q1_source ? QA_COMPOSITION_Q1_HIPNOTIC_SUPPLY_PROFILE : q2_source ? QA_COMPOSITION_Q2_HIPNOTIC_SUPPLY_PROFILE : QA_COMPOSITION_Q3_HIPNOTIC_SUPPLY_PROFILE;
  else if (q1_source) {
    const qa_pickup_supply_profile *q1q3 = &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE;
    record->identity_ammo = calloc(q1q3->ammo_count + 1, sizeof *record->identity_ammo);
    record->identity_weapons = calloc(q1q3->weapon_count + 1, sizeof *record->identity_weapons);
    if (record->identity_ammo == NULL || record->identity_weapons == NULL) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_free, record->identity_ammo); ss_own(sim, ss_dispose_free, record->identity_weapons);
    for (size_t i = 0; i < q1q3->ammo_count; i++)
      record->identity_ammo[i] = (qa_pickup_supply_mapping){q1q3->ammo[i].source, &q1q3->ammo[i].source, 1};
    for (size_t i = 0; i < q1q3->weapon_count; i++)
      record->identity_weapons[i] = (qa_pickup_supply_mapping){q1q3->weapons[i].source, &q1q3->weapons[i].source, 1};
    memset(&base, 0, sizeof base);
    base.id = "composition:q1-q1-supply"; base.ammo = record->identity_ammo; base.ammo_count = q1q3->ammo_count;
    base.weapons = record->identity_weapons; base.weapon_count = q1q3->weapon_count;
  } else base = q2_source ? QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE : QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE;
  qa_composition_expansion_supply_kind expansions[1];
  size_t expansion_count = 0;
  if (program == QA_COMP_SELECTED_Q1_ROGUE) expansions[expansion_count++] = QA_COMPOSITION_EXPANSION_Q1_ROGUE;
  else if (program == QA_COMP_SELECTED_Q1_MG3) expansions[expansion_count++] = QA_COMPOSITION_EXPANSION_Q1_MG3;
  if (!qa_composition_expansion_supply(&base, expansions, expansion_count, &record->expanded)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_owned_profile, &record->expanded);
  qa_pickup_supply_profile expanded = qa_composition_owned_profile_view(&record->expanded);
  if (!qa_composition_expansion_source_supply(&expanded, &record->profile)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_owned_profile, &record->profile);
  record->view = qa_composition_owned_profile_view(&record->profile);
  /* replacedItems */
  size_t replaced_count = 0;
  qa_item_id *replaced = NULL;
  if (!(q1_source || sim->strategy->owns_supply(sim))) replaced = qa_ss_replaced_supply_items(&record->view, &replaced_count);
  size_t extra_count = 0; const qa_item_id *extra = sim->strategy->extra_replaced_items(sim, &extra_count);
  record->replaced = realloc(replaced, (replaced_count + extra_count + 1) * sizeof *record->replaced);
  if (record->replaced == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->replaced);
  for (size_t i = 0; i < extra_count; i++) record->replaced[replaced_count++] = extra[i];
  qa_app_q1_selected_arsenal_options arsenal; memset(&arsenal, 0, sizeof arsenal);
  arsenal.game = game; arsenal.ctx = record; arsenal.impulse = ss_q1_impulse; arsenal.prepare_pickup = ss_q1_prepare_pickup;
  if (sim->strategy->as_q1(sim) != NULL || sim->strategy->as_quakec(sim) != NULL) arsenal.native_player = ss_q1_native_player;
  arsenal.fired = ss_q1_fired; arsenal.replaced_items = record->replaced; arsenal.replaced_item_count = replaced_count;
  arsenal.observe = ss_q1_observe;
  memset(out, 0, sizeof *out);
  out->family = "q1"; out->q1 = qa_app_q1_selected_arsenal_create(arsenal);
  if (out->q1 == NULL) qa_range_error("Out of memory");
  record->arsenal = out->q1;
  qa_app_shared_sim_bind_selected_ammo(sim, &record->view);
  qa_shared_pickup_admission_options admission; memset(&admission, 0, sizeof admission);
  admission.inventory = sim->inventory; admission.profile = record->view; admission.ctx = record;
  admission.ammo_granted = ss_q1_ammo_granted; admission.weapon_granted = ss_q1_weapon_granted;
  sim->selected_supply = qa_shared_pickup_admission_create(admission);
  if (sim->selected_supply == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_admission, sim->selected_supply);
  record->admission = qa_shared_pickup_admission_as_contract(sim->selected_supply);
  sim->strategy->install_pickup_admission(sim, &record->admission);
  qa_app_shared_sim_prepare_selected_original_pickups(sim, &record->view);
}

/* ---- createSelectedQ3Arsenal (2096-2246) ---- */
typedef struct ss_q3_selected {
  qa_app_shared_simulation *sim;
  qa_q3_config_string_registry config;
  qa_weapon_behavior_projectile_port port;
  qa_app_q3_selected_source *source;
  qa_app_q3_selected_arsenal *arsenal;
  qa_composition_owned_profile expanded, profile; qa_pickup_supply_profile view; bool has_profile;
  qa_pickup_supply_mapping *identity_ammo, *identity_weapons; qa_pickup_supply_owner *identity_owners;
  qa_item_id *replaced; qa_inventory_entry *loadout_ammo; qa_inventory_entry *spawn_ammo;
  qa_pickup_admission admission;
} ss_q3_selected;
/* ConfigStringRegistry({ get: index => strings.get(index) ?? "", set }). */
static const char *ss_q3_string_get(void *ctx, double index) {
  char **value = qa_ss_map_get(&((ss_q3_selected *)ctx)->sim->selected_q3_strings, &index);
  return value != NULL ? *value : "";
}
static void ss_q3_string_set(void *ctx, double index, const char *value) {
  qa_ss_map *strings = &((ss_q3_selected *)ctx)->sim->selected_q3_strings;
  char **existing = qa_ss_map_get(strings, &index);
  char *copy = qa_ss_strdup(value);
  if (copy == NULL) qa_range_error("Out of memory");
  if (existing != NULL) free(*existing);
  qa_ss_map_set(strings, &index, &copy);
}
/* gameType(): the selected Q3 source's g_gametype. */
static double ss_q3_game_type(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  double own;
  if (sim->strategy->q3_game_type(sim, &own)) return own;
  const char *match = sim->recipe.match.provider;
  return qa_ss_is(match, "q1:ctf") || qa_ss_is(match, "q2:ctf") || qa_ss_is(match, "q2:lmctf") ? QA_Q3_BASE_GT_CTF
      : qa_app_shared_sim_team_game(sim) ? QA_Q3_BASE_GT_TEAM : QA_Q3_BASE_GT_FFA;
}
static void ss_q3_print_message(void *ctx, const char *text) { qa_app_shared_sim_print(((ss_q3_selected *)ctx)->sim, text, NULL); }
/* weaponEffects */
static double ss_q3_effect_damage(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->equipment_damage_factor(sim, actor);
}
static double ss_q3_effect_delay(void *ctx, qa_actor_id actor, double milliseconds, double persistent) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->equipment_firing_delay(sim, actor, milliseconds, persistent);
}
/* Q3Strategy.equipmentDamageFactor / equipmentFiringDelay */
double qa_ss_q3_equipment_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor) {
  if (ss_q3_native_client(sim, actor) == NULL) ss_fail("Primary Q3 equipment lost its client");
  double factor;
  sim->strategy->weapon_damage_factor(sim, actor, true, &factor);
  return factor;
}
double qa_ss_q3_equipment_firing_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent) {
  (void)persistent; /* TS Q3Strategy.equipmentFiringDelay(actor, milliseconds) reads the client's own persistent powerup */
  qa_q3_game_client *client = ss_q3_native_client(sim, actor);
  if (client == NULL) ss_fail("Primary Q3 equipment lost its client");
  double tag = client->persistant_powerup != NULL && client->persistant_powerup->item != NULL ? client->persistant_powerup->item->tag : 0;
  return qa_q3_weapon_delay(milliseconds, tag, ss_q3_powerup(client, QA_Q3_PW_HASTE) != 0 || qa_app_shared_sim_lmctf_haste(sim, actor));
}
/* Q3SelectedSourceHost members */
static const char *ss_q3_userinfo(void *ctx, qa_actor_id actor) {
  const char *userinfo = qa_app_shared_sim_source_player_userinfo(((ss_q3_selected *)ctx)->sim, actor);
  return userinfo != NULL ? userinfo : "";
}
static double ss_q3_now(void *ctx) { return ((ss_q3_selected *)ctx)->sim->selected_q3_time; }
static qa_owned_actor ss_q3_world_actor(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  qa_actor_id actor; qa_owned_actor owner;
  if (!qa_app_shared_sim_world_actor(sim, &actor) || !qa_app_shared_sim_resolve_owned(sim, actor, &owner)) ss_fail("Selected Q3 source has no map world");
  return owner;
}
static qa_q3_base_team ss_q3_team_of(const char *team) {
  if (team == NULL) return QA_Q3_BASE_TEAM_FREE;
  char lower[64]; size_t length = strlen(team);
  if (length >= sizeof lower) return QA_Q3_BASE_TEAM_FREE;
  for (size_t i = 0; i <= length; i++) lower[i] = (char)(team[i] >= 'A' && team[i] <= 'Z' ? team[i] - 'A' + 'a' : team[i]);
  if (strcmp(lower, "team:red") == 0 || strcmp(lower, "red") == 0 || strcmp(lower, "5") == 0 || strcmp(lower, "q3:1") == 0 || strcmp(lower, "q2:1") == 0)
    return QA_Q3_BASE_TEAM_RED;
  if (strcmp(lower, "team:blue") == 0 || strcmp(lower, "blue") == 0 || strcmp(lower, "14") == 0 || strcmp(lower, "q3:2") == 0 || strcmp(lower, "q2:2") == 0)
    return QA_Q3_BASE_TEAM_BLUE;
  return QA_Q3_BASE_TEAM_FREE;
}
static bool ss_q3_player_pose(void *ctx, qa_actor_id actor, qa_app_q3_selected_client_pose *out) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  qa_ss_arsenal_observation player;
  if (!qa_app_shared_sim_arsenal_observation(sim, actor, &player)) return false;
  qa_app_source_selected_player_stats stats = sim->strategy->selected_player(sim, actor);
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  memset(out, 0, sizeof *out);
  out->angles = player.view_angles; out->view_height = player.view_height;
  out->max_health = stats.max_health;
  out->team = stats.has_team ? (qa_q3_base_team)stats.team : ss_q3_team_of(combat.has_value ? combat.value.team : NULL);
  out->quad_until = stats.quad_until; out->haste_until = stats.haste_until;
  return true;
}
/* SourceStrategy.selectedPlayer per class. */
qa_app_source_selected_player_stats qa_ss_loading_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor;
  return (qa_app_source_selected_player_stats){100, false, 0, 0, 0};
}
qa_app_source_selected_player_stats qa_ss_q1_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  return (qa_app_source_selected_player_stats){player != NULL ? player->max_health : 100, false, 0,
      trunc((player != NULL ? qa_q1_player_powerup_get(player, "quad") : 0) * 1000), 0};
}
qa_app_source_selected_player_stats qa_ss_quakec_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_quakec_equipment equipment = qa_app_sim_quakec_source_client_equipment(sim->source.quakec, actor);
  return (qa_app_source_selected_player_stats){equipment.max_health, false, 0, trunc(equipment.quad_until * 1000), 0};
}
qa_app_source_selected_player_stats qa_ss_q2_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_entity *entity = qa_q2_game_entity_of(sim->source.product->services, &actor);
  double max_health = entity != NULL ? entity->max_health : 100;
  return (qa_app_source_selected_player_stats){max_health, false, 0,
      trunc(qa_q2_item_module_player_powerups(sim->source.product->items, actor).quad_until * 1000), 0};
}
qa_app_source_selected_player_stats qa_ss_q2_native_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_compat_q2_native_primary_player native = qa_app_shared_sim_selected_native_player(sim);
  return (qa_app_source_selected_player_stats){qa_compat_q2_native_primary_player_max_health(&native, actor), false, 0, 0, 0};
}
qa_app_source_selected_player_stats qa_ss_q3_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_client *client = ss_q3_native_client(sim, actor);
  if (client == NULL) return (qa_app_source_selected_player_stats){100, false, 0, 0, 0};
  qa_q3_base_product product = qa_q3_base_player_state_record_product(&client->ps);
  return (qa_app_source_selected_player_stats){qa_q3_base_player_state_slots_get(&client->ps.stats, qa_q3_base_stat_schema_for(product)->max_health),
      true, client->sess.session_team, ss_q3_powerup(client, QA_Q3_PW_QUAD), ss_q3_powerup(client, QA_Q3_PW_HASTE)};
}
qa_app_source_selected_player_stats qa_ss_q3_qvm_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_qvm_primary_weapons *weapons = qa_app_shared_sim_require_qvm_weapons(sim);
  return (qa_app_source_selected_player_stats){qa_qvm_primary_weapons_max_health(weapons, actor), false, 0,
      qa_qvm_primary_weapons_powerup_until(weapons, actor, QA_QVM_PRIMARY_POWERUP_QUAD),
      qa_qvm_primary_weapons_powerup_until(weapons, actor, QA_QVM_PRIMARY_POWERUP_HASTE)};
}
static qa_victim_armor_context ss_q3_armor_context(void *ctx, qa_damage_request request) {
  return qa_app_shared_sim_victim_armor_context(((ss_q3_selected *)ctx)->sim, request);
}
static void ss_q3_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  qa_app_sim_shared_physics_set_collision(((ss_q3_selected *)ctx)->sim->physics, actor, collision);
}
static bool ss_q3_curves(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->curves_enabled(sim);
}
static bool ss_q3_player_curve_clip(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->player_curve_clip(sim);
}
static bool ss_q3_friendly_fire(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->friendly_fire(sim);
}
static double ss_q3_knockback(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->knockback(sim);
}
static double ss_q3_intermission_queued(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->intermission_queued(sim);
}
static void ss_q3_check_hurt_carrier(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  sim->strategy->check_hurt_carrier(sim, qa_q3_game_entity_actor(target).id, qa_q3_game_entity_actor(attacker).id);
}
static bool ss_q3_check_obelisk_attack(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->check_obelisk_attack(sim, qa_q3_game_entity_actor(target).id, qa_q3_game_entity_actor(attacker).id);
}
/* Q3Strategy.checkHurtCarrier / checkObeliskAttack */
void qa_ss_q3_check_hurt_carrier(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker) {
  qa_q3_base_entity_records *records = qa_app_q3_source_runtime_records(sim->source.q3);
  qa_q3_game_entity *native_target = qa_q3_base_entity_records_native_by_actor(records, true, target);
  qa_q3_game_entity *native_attacker = qa_q3_base_entity_records_native_by_actor(records, true, attacker);
  if (native_target != NULL && native_attacker != NULL) qa_q3_ta_team_runtime_check_hurt_carrier(qa_app_q3_source_runtime_team(sim->source.q3), native_target, native_attacker);
}
bool qa_ss_q3_check_obelisk_attack(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker) {
  qa_q3_base_entity_records *records = qa_app_q3_source_runtime_records(sim->source.q3);
  qa_q3_game_entity *native_target = qa_q3_base_entity_records_native_by_actor(records, true, target);
  qa_q3_game_entity *native_attacker = qa_q3_base_entity_records_native_by_actor(records, true, attacker);
  return native_target != NULL && native_attacker != NULL
      && qa_q3_ta_team_runtime_check_obelisk_attack(qa_app_q3_source_runtime_team(sim->source.q3), native_target, native_attacker);
}
static double ss_q3_quad_factor(void *ctx, const qa_actor_id *actor) {
  (void)actor; /* TS quadFactor: () => ...: the actor argument is unused there too */
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->quad_factor(sim);
}
static double ss_q3_proximity_timeout(void *ctx) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  return sim->strategy->proximity_timeout(sim);
}
static double ss_q3_model_index(void *ctx, const char *path) { return qa_q3_config_string_registry_model_index(&((ss_q3_selected *)ctx)->config, path); }
static double ss_q3_sound_index(void *ctx, const char *path) { return qa_q3_config_string_registry_sound_index(&((ss_q3_selected *)ctx)->config, path); }
/* execute(actor, step): the q3-source actor execution record. */
typedef struct ss_q3_execution { qa_app_q3_selected_source *source; qa_owned_actor actor; qa_app_q3_selected_source_step_fn step; void *step_ctx; } ss_q3_execution;
static void ss_q3_execution_step(void *ctx, double previous, double time) { ss_q3_execution *e = ctx; e->step(e->step_ctx, previous, time); }
static qa_q2_motion ss_q3_execution_motion(void *ctx, qa_body_state body) { ss_q3_execution *e = ctx; return qa_app_q3_selected_source_motion(e->source, e->actor, body); }
static qa_app_sim_shared_solid ss_q3_execution_collision(void *ctx) { ss_q3_execution *e = ctx; return qa_app_q3_selected_source_collision(e->source, e->actor); }
static void ss_q3_execute(void *ctx, qa_owned_actor actor, qa_app_q3_selected_source_step_fn step, void *step_ctx) {
  ss_q3_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  ss_q3_execution *execution = qa_app_shared_sim_keep(sim, sizeof *execution);
  execution->source = record->source; execution->actor = actor; execution->step = step; execution->step_ctx = step_ctx;
  qa_app_sim_actor_execution entry; memset(&entry, 0, sizeof entry);
  entry.kind = QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE; entry.actor = actor; entry.provider = sim->weapon_provider.provider;
  entry.content = sim->weapon_provider.content; entry.ctx = execution;
  entry.step = ss_q3_execution_step; entry.motion = ss_q3_execution_motion; entry.collision = ss_q3_execution_collision;
  qa_app_shared_sim_register_actor_execution(sim, entry);
}
static void ss_q3_event(void *ctx, qa_owned_actor actor, const qa_q3_base_entity_state *state, double time) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) return;
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_ENTITY;
  event.entity.kind = "entity-event"; event.entity.actor = actor.id; event.entity.state = *state; event.entity.origin = body.origin; event.entity.time = time;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q3-source"; input.q3_source = &event;
  qa_source_time at = ss_milliseconds(time);
  qa_app_shared_sim_emit(sim, sim->weapon_provider.content, input, &at, NULL);
}
static void ss_q3_client_changed(void *ctx, qa_owned_actor actor, qa_app_q3_selected_client_effects before, qa_app_q3_selected_client_effects after) {
  qa_app_shared_sim_selected_q3_client_changed(((ss_q3_selected *)ctx)->sim, actor, before, after);
}
static void ss_q3_drop_objectives(void *ctx, qa_owned_actor actor) { qa_app_shared_sim_drop_selected_q3_objectives(((ss_q3_selected *)ctx)->sim, actor); }
static void ss_q3_spawn_point(void *ctx, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_app_shared_sim_selected_q3_spawn(((ss_q3_selected *)ctx)->sim, actor, out_origin, out_angles);
}
static void ss_q3_return_pickup(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q3_selected *)ctx)->sim;
  sim->strategy->return_pickup(sim, actor);
}
/* Q3Strategy.returnPickup */
void qa_ss_q3_return_pickup(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_entity *pickup = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  if (pickup == NULL || !qa_q3_game_entity_inuse(pickup)) ss_fail("Borrowed persistent pickup was retired");
  qa_q3_return_persistent_powerup(pickup, qa_app_q3_source_runtime_world(sim->source.q3));
}
/* this.weaponBehavior as the projectile port. */
static bool ss_q3_port_controls(void *ctx, qa_actor_id projectile) { return qa_app_sim_simulation_weapon_behaviors_controls_trajectory(ctx, projectile); }
static qa_weapon_trajectory_update_result ss_q3_port_launch(void *ctx, qa_weapon_behavior_launch input) { return qa_app_sim_simulation_weapon_behaviors_launch(ctx, input); }
static qa_weapon_trajectory_update_result ss_q3_port_step(void *ctx, qa_owned_actor projectile, qa_body_state body, double time_seconds) {
  return qa_app_sim_simulation_weapon_behaviors_step(ctx, projectile, body, time_seconds);
}
/* Q3SelectedArsenal options */
static double ss_q3_arsenal_firing_delay(void *ctx, qa_owned_actor actor, double milliseconds) {
  return qa_app_q3_selected_source_firing_delay(((ss_q3_selected *)ctx)->source, actor, milliseconds);
}
static qa_arsenal_state ss_q3_qvm_loadout(void *ctx, qa_owned_actor actor, qa_arsenal_state defaults) {
  ss_q3_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  if (sim->strategy->as_q3_qvm(sim) == NULL || !qa_ss_is(defaults.state.kind, "q3")) ss_fail("Selected source loadout lost its QVM player");
  ss_qvm_state player; ss_qvm_actor_state(sim, actor.id, &player);
  size_t inventory_count = 0;
  const qa_inventory_entry *inventory = qa_shared_inventory_table_entries(sim->inventory, actor.id, &inventory_count);
  const qa_q3_weapon_item *active = qa_q3_get_weapon_item(player.state.weapon);
  qa_arsenal_state state = defaults;
  state.active_weapon = active != NULL ? active->item : NULL;
  state.state.source_weapon = player.state.weapon;
  /* The admitted arsenal keeps this array for the player's life: kept until the simulation is destroyed. */
  qa_inventory_entry *ammo = qa_app_shared_sim_keep(sim, (defaults.ammo_count ? defaults.ammo_count : 1) * sizeof *ammo);
  for (size_t i = 0; i < defaults.ammo_count; i++) {
    const qa_inventory_entry *found = NULL;
    for (size_t j = 0; j < inventory_count && found == NULL; j++) if (qa_ss_is(inventory[j].item, defaults.ammo[i].item)) found = &inventory[j];
    if (found != NULL) ammo[i] = *found; else { ammo[i] = defaults.ammo[i]; ammo[i].count = 0; }
  }
  state.ammo = ammo;
  return state;
}
static qa_app_q3_equipment_state ss_q3_equipment_read(void *ctx, qa_owned_actor actor) { return qa_app_q3_selected_source_equipment(((ss_q3_selected *)ctx)->source, actor); }
static void ss_q3_equipment_consume(void *ctx, qa_owned_actor actor, double item) { qa_app_q3_selected_source_consume(((ss_q3_selected *)ctx)->source, actor, item); }
static void ss_q3_equipment_restore(void *ctx, qa_owned_actor actor, qa_app_q3_equipment_state state) {
  qa_app_q3_selected_source_restore_equipment(((ss_q3_selected *)ctx)->source, actor, state);
}
static void ss_q3_equipment_advance(void *ctx, qa_owned_actor actor, double milliseconds) { qa_app_q3_selected_source_advance_movement(((ss_q3_selected *)ctx)->source, actor, milliseconds); }
static void ss_q3_equipment_end(void *ctx, qa_owned_actor actor, double milliseconds) { qa_app_q3_selected_source_end_command(((ss_q3_selected *)ctx)->source, actor, milliseconds); }
static void ss_q3_fire(void *ctx, qa_owned_actor actor, double weapon, qa_weapon_step_input input) {
  ss_q3_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_app_q3_selected_source_fire(record->source, actor, weapon, input);
  if (!qa_session_actor_registry_is_live(sim->actors, actor.id)) return;
  if (sim->strategy->animates_weapon_fire(sim)) qa_app_shared_sim_weapon_character_animation(sim, actor.id, "attack", weapon == 1, sim->weapon_provider.content, false);
  else {
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor.id);
    if (player != NULL && player->character == QA_GAME_FAMILY_Q2)
      qa_app_shared_sim_weapon_character_animation(sim, actor.id, "attack", weapon == 1, sim->weapon_provider.content, false);
  }
  const qa_q3_weapon_item *item = qa_q3_get_weapon_item(weapon);
  sim->strategy->note_fired(sim, actor.id, item != NULL ? item->item : NULL);
  qa_body_state body;
  if (qa_app_shared_sim_body(sim, actor.id, &body)) sim->strategy->weapon_noise(sim, actor.id, body.origin, "weapon");
}
static void ss_q3_use_holdable(void *ctx, qa_owned_actor actor, double event, qa_weapon_step_input input) {
  (void)input; /* TS useHoldable: (actor, event) => source.useHoldable(actor, event) */
  qa_app_q3_selected_source_use_holdable(((ss_q3_selected *)ctx)->source, actor, event);
}
/* SharedPickupAdmission grants */
static void ss_q3_ammo_granted(void *ctx, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  ss_q3_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  qa_app_q3_selected_arsenal_pickup_ammo(record->arsenal, actor, grants, count, auto_switch && sim->strategy->allows_auto_switch(sim, actor.id));
  qa_app_shared_sim_refresh_arsenal_cache(sim, actor.id);
}
static void ss_q3_weapon_granted(void *ctx, qa_owned_actor actor, const qa_item_id *weapons, size_t count, qa_pickup_selection selection) {
  ss_q3_selected *record = ctx;
  qa_app_q3_selected_arsenal_pickup_weapons(record->arsenal, actor, weapons, count, selection);
  qa_app_shared_sim_refresh_arsenal_cache(record->sim, actor.id);
}
static qa_arsenal_state ss_q3_spawn_loadout_fn(void *ctx, qa_provider_id provider, const char *product, bool team_deathmatch) {
  ss_q3_selected *record = ctx;
  qa_arsenal_state loadout = qa_q3_spawn_loadout(provider, product, team_deathmatch);
  free(record->spawn_ammo); record->spawn_ammo = (qa_inventory_entry *)loadout.ammo;
  return loadout;
}

void qa_app_shared_sim_create_selected_q3_arsenal(qa_app_shared_simulation *sim, qa_ss_selected_arsenal *out) {
  const char *product = strstr(sim->weapon_provider.content, ":missionpack:") != NULL ? "missionpack" : "baseq3";
  qa_clock_profile timing = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider).clock;
  if (timing.kind != QA_CLOCK_Q3) ss_fail("Selected Q3 requires its source clock");
  sim->selected_q3_time = trunc(sim->selected_milliseconds);
  sim->selected_q3_next = sim->selected_q3_time + timing.server_frame_milliseconds;
  ss_q3_selected *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim;
  qa_q3_config_string_store store = {record, ss_q3_string_get, ss_q3_string_set};
  qa_q3_config_string_registry_init(&record->config, store);
  ss_q3_string_set(record, 20, "baseq3-1");
  { char text[32]; ss_q3_string_set(record, 21, qa_ss_number_text(sim->selected_q3_time, text)); }
  char *server_info = qa_ss_strdup("");
  char *source_info = sim->strategy->q3_server_info(sim);
  if (server_info == NULL || source_info == NULL) qa_range_error("Out of memory");
  char *map_name = qa_ss_map_name(sim->recipe.map.geometry.requested_path);
  char game_type[32], max_clients[32];
  qa_ss_number_text(ss_q3_game_type(record), game_type);
  qa_ss_number_text(sim->options.max_clients, max_clients);
  const char *pairs[3][2] = {{"mapname", map_name}, {"g_gametype", game_type}, {"sv_maxclients", max_clients}};
  qa_info_options info = {QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_SERVER_INFO, true, ss_q3_print_message, record};
  for (size_t i = 0; i < 3; i++) {
    char *next = qa_set_info_value(server_info[0] != '\0' ? server_info : source_info, pairs[i][0], pairs[i][1], &info);
    free(server_info); server_info = next;
  }
  free(source_info); free(map_name);
  ss_q3_string_set(record, 0, server_info);
  free(server_info);
  bool primary_equipment = sim->strategy->primary_equipment(sim);
  bool weapon_effects = sim->strategy->has_equipment_effects(sim);
  record->port = (qa_weapon_behavior_projectile_port){sim->weapon_behavior, ss_q3_port_controls, ss_q3_port_launch, ss_q3_port_step};
  qa_app_q3_selected_source_host host; memset(&host, 0, sizeof host);
  host.ctx = record;
  host.actors = sim->actors; host.bodies = ss_bodies(sim); host.callbacks = sim->callbacks; host.combat = sim->combat; host.inventory = sim->inventory;
  host.has_source_damage_modifier = sim->strategy->damage_modifier(sim, &host.source_damage_modifier);
  host.queries = sim->scene; host.weapon_behavior = &record->port; host.provider = sim->weapon_provider.provider;
  host.product = qa_ss_is(product, "missionpack") ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  host.equipment_source = !primary_equipment;
  if (weapon_effects) { host.has_weapon_effects = true; host.weapon_damage_factor = ss_q3_effect_damage; host.weapon_firing_delay = ss_q3_effect_delay; }
  host.content = sim->weapon_provider.content; host.configstrings = record->config.store; host.userinfo = ss_q3_userinfo;
  host.max_clients = sim->options.max_clients; host.seed = sim->options.seed; host.now = ss_q3_now; host.world_actor = ss_q3_world_actor;
  host.player = ss_q3_player_pose;
  host.combat_provider = sim->recipe.combat.provider; host.inventory_provider = sim->recipe.inventory.provider; host.movement_provider = sim->recipe.movement.provider;
  host.armor_context = ss_q3_armor_context; host.collision = ss_q3_collision; host.curves = ss_q3_curves; host.player_curve_clip = ss_q3_player_curve_clip;
  host.game_type = ss_q3_game_type; host.friendly_fire = ss_q3_friendly_fire; host.knockback = ss_q3_knockback;
  host.intermission_queued = ss_q3_intermission_queued; host.check_hurt_carrier = ss_q3_check_hurt_carrier;
  host.check_obelisk_attack = ss_q3_check_obelisk_attack; host.quad_factor = ss_q3_quad_factor; host.proximity_timeout = ss_q3_proximity_timeout;
  host.model_index = ss_q3_model_index; host.sound_index = ss_q3_sound_index; host.print = ss_q3_print_message; host.execute = ss_q3_execute;
  host.event = ss_q3_event; host.client_changed = ss_q3_client_changed; host.drop_objectives = ss_q3_drop_objectives;
  host.spawn_point = ss_q3_spawn_point; host.return_pickup = ss_q3_return_pickup;
  record->source = qa_app_q3_selected_source_create(host, NULL);
  if (record->source == NULL) qa_range_error("Out of memory");
  sim->selected_q3_source = record->source;
  /* identity: the Q3 base weapons (weapon <= 10) supplying themselves. */
  record->identity_ammo = calloc(QA_Q3_WEAPON_ITEM_COUNT, sizeof *record->identity_ammo);
  record->identity_weapons = calloc(QA_Q3_WEAPON_ITEM_COUNT, sizeof *record->identity_weapons);
  record->identity_owners = calloc(QA_Q3_WEAPON_ITEM_COUNT, sizeof *record->identity_owners);
  if (record->identity_ammo == NULL || record->identity_weapons == NULL || record->identity_owners == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->identity_ammo); ss_own(sim, ss_dispose_free, record->identity_weapons); ss_own(sim, ss_dispose_free, record->identity_owners);
  qa_pickup_supply_profile identity; memset(&identity, 0, sizeof identity);
  identity.id = "composition:q3-base-q3-supply";
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
    const qa_q3_weapon_item *weapon = &qa_q3_weapon_items[i];
    if (weapon->weapon > 10) continue;
    if (weapon->ammo != NULL) {
      record->identity_owners[identity.ammo_owner_count++] = (qa_pickup_supply_owner){weapon->ammo, weapon->ammo};
      bool seen = false;
      for (size_t j = 0; j < identity.ammo_count && !seen; j++) seen = qa_ss_is(record->identity_ammo[j].source, weapon->ammo);
      if (!seen) record->identity_ammo[identity.ammo_count++] = (qa_pickup_supply_mapping){weapon->ammo, &weapon->ammo, 1};
    }
    record->identity_weapons[identity.weapon_count++] = (qa_pickup_supply_mapping){weapon->item, &weapon->item, 1};
  }
  identity.ammo = record->identity_ammo; identity.ammo_owners = record->identity_owners; identity.weapons = record->identity_weapons;
  const char *family = sim->strategy->supply_family(sim);
  const qa_pickup_supply_profile *base = qa_ss_is(family, "q1") ? &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE
      : qa_ss_is(family, "q2") ? &QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE
      : sim->strategy->identity_supply(sim) ? &identity : NULL;
  qa_app_q3_selected_arsenal_options options; memset(&options, 0, sizeof options);
  options.provider = sim->weapon_provider.provider; options.product = product; options.inventory = qa_app_shared_sim_inventory_access(sim);
  if (base != NULL) {
    qa_composition_expansion_supply_kind expansions[1] = {QA_COMPOSITION_EXPANSION_Q3_MISSIONPACK};
    if (!qa_composition_expansion_supply(base, expansions, qa_ss_is(product, "missionpack") ? 1 : 0, &record->expanded)) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_owned_profile, &record->expanded);
    qa_pickup_supply_profile expanded = qa_composition_owned_profile_view(&record->expanded);
    if (!qa_composition_expansion_source_supply(&expanded, &record->profile)) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_owned_profile, &record->profile);
    record->view = qa_composition_owned_profile_view(&record->profile); record->has_profile = true;
    options.has_supply = true; options.supply_profile = record->view.id;
    if (qa_ss_is(family, "q1")) {
      record->loadout_ammo = calloc(64, sizeof *record->loadout_ammo);
      if (record->loadout_ammo == NULL) qa_range_error("Out of memory");
      ss_own(sim, ss_dispose_free, record->loadout_ammo);
      if (!qa_composition_q1_q3_supply_loadout(sim->weapon_provider.provider, product, ss_q3_spawn_loadout_fn, record, record->loadout_ammo, 64,
          &options.supply_loadout)) qa_range_error("Out of memory");
      free(record->spawn_ammo); record->spawn_ammo = NULL;
    } else {
      options.supply_loadout = qa_q3_spawn_loadout(sim->weapon_provider.provider, product, false);
      ss_own(sim, ss_dispose_free, (void *)options.supply_loadout.ammo);
    }
    size_t replaced_count = 0;
    qa_item_id *replaced = NULL;
    size_t extra_count = 0; const qa_item_id *extra = NULL;
    if (!sim->strategy->owns_supply(sim)) {
      replaced = qa_ss_replaced_supply_items(&record->view, &replaced_count);
      extra = sim->strategy->extra_replaced_items(sim, &extra_count);
    }
    record->replaced = realloc(replaced, (replaced_count + extra_count + 1) * sizeof *record->replaced);
    if (record->replaced == NULL) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_free, record->replaced);
    for (size_t i = 0; i < extra_count; i++) record->replaced[replaced_count++] = extra[i];
    options.supply_replaced_items = record->replaced; options.supply_replaced_item_count = replaced_count;
  }
  options.ctx = record;
  if (sim->strategy->identity_supply(sim)) options.loadout = ss_q3_qvm_loadout;
  options.firing_delay = ss_q3_arsenal_firing_delay;
  options.has_equipment = true;
  options.equipment.ctx = record; options.equipment.has_owns_holdables = true;
  options.equipment.owns_holdables = qa_app_q3_selected_source_owns_equipment(record->source);
  options.equipment.read = ss_q3_equipment_read; options.equipment.consume = ss_q3_equipment_consume;
  options.equipment.restore = ss_q3_equipment_restore; options.equipment.advance = ss_q3_equipment_advance; options.equipment.end_command = ss_q3_equipment_end;
  options.fire = ss_q3_fire; options.use_holdable = ss_q3_use_holdable;
  memset(out, 0, sizeof *out);
  out->family = "q3"; out->q3 = qa_app_q3_selected_arsenal_create(options);
  if (out->q3 == NULL) qa_range_error("Out of memory");
  record->arsenal = out->q3;
  if (record->has_profile) {
    qa_app_shared_sim_bind_selected_ammo(sim, &record->view);
    qa_shared_pickup_admission_options admission; memset(&admission, 0, sizeof admission);
    admission.inventory = sim->inventory; admission.profile = record->view; admission.ctx = record;
    admission.ammo_granted = ss_q3_ammo_granted; admission.weapon_granted = ss_q3_weapon_granted;
    sim->selected_supply = qa_shared_pickup_admission_create(admission);
    if (sim->selected_supply == NULL) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_admission, sim->selected_supply);
    record->admission = qa_shared_pickup_admission_as_contract(sim->selected_supply);
    sim->strategy->install_pickup_admission(sim, &record->admission);
    qa_app_shared_sim_prepare_selected_original_pickups(sim, &record->view);
  }
}

/* ---- createSelectedQ2Arsenal (2484-2580) ---- */
typedef struct ss_q2_selected {
  ss_q2w_record weapon_hooks; /* first member: the Q2 weapon hooks read it as an ss_q2w_record */
  qa_app_shared_simulation *sim;
  qa_q2_entity_services *game; qa_q2w_weapons *weapons;
  qa_comp_selected_q2_mission_weapons *mission;
  qa_app_q2_inventory_definition *definitions;
  qa_pickup_supply_mapping *identity_ammo, *identity_weapons;
  qa_composition_owned_profile expanded, profile; qa_pickup_supply_profile view;
  qa_item_id *replaced; qa_app_q2_loadout_entry *loadout;
  qa_app_q2_selected_arsenal *arsenal;
  int64_t release_subscription;
  qa_pickup_admission admission;
} ss_q2_selected;
static qa_q2m_state *ss_q2_no_monster(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return NULL; } /* TS () => undefined */
/* Q2Weapons hooks */
static void ss_q2_selected_emit(void *ctx, qa_q2w_event event) {
  ss_q2_selected *record = ctx; qa_app_shared_simulation *sim = record->sim;
  if (event.kind == QA_Q2W_EVENT_MUZZLEFLASH && event.actor != NULL) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, *event.actor);
    if (player == NULL || player->character != QA_GAME_FAMILY_Q2)
      qa_app_shared_sim_weapon_character_animation(sim, *event.actor, "attack", false, sim->weapon_provider.content, false);
  }
  qa_app_shared_sim_weapon_event(sim, sim->weapon_provider.content, &event);
}
static void ss_q2_selected_noise(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  qa_app_shared_simulation *sim = ((ss_q2_selected *)ctx)->sim;
  sim->strategy->monster_noise(sim, actor, origin, secondary);
  for (size_t i = 0; i < sim->monster_sources.count; i++) {
    qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    if (qa_ss_is(source->kind, "q2")) qa_q2m_monsters_report_noise(&source->q2->monsters, actor, origin, secondary);
  }
}
static double ss_q2_selected_interval(void *ctx, qa_actor_id actor, double seconds) {
  return qa_app_shared_sim_selected_weapon_delay(((ss_q2_selected *)ctx)->sim, actor, seconds);
}
/* registerSelectedQ2MissionWeapons hooks */
static double ss_q2_mission_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((ss_q2_selected *)ctx)->sim->physics); }
static struct qa_q2m_context *ss_q2_mission_monster(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2_selected *)ctx)->sim;
  if (ss_source_is(sim, QA_SS_SOURCE_Q2)) { qa_q2m_context *found = qa_q2m_monsters_context(&sim->source.product->monsters, actor); if (found != NULL) return found; }
  for (size_t i = 0; i < sim->monster_sources.count; i++) {
    qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    if (!qa_ss_is(source->kind, "q2")) continue;
    qa_q2m_context *found = qa_q2m_monsters_context(&source->q2->monsters, actor);
    if (found != NULL) return found;
  }
  return NULL;
}
static void ss_q2_mission_player_effect(void *ctx, qa_q2mp_player_effect effect) {
  qa_app_shared_simulation *sim = ((ss_q2_selected *)ctx)->sim;
  qa_q2_composition_event composition; memset(&composition, 0, sizeof composition);
  composition.kind = QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER; composition.player_effect = &effect;
  qa_app_sim_source_event_input input; memset(&input, 0, sizeof input);
  input.kind = "q2-composition"; input.q2_composition = &composition;
  qa_source_time time = ss_selected_q2(sim)->frame.time;
  qa_app_shared_sim_emit(sim, sim->weapon_provider.content, input, &time, NULL);
}
/* Q2SelectedArsenal options: observe and the game.host reads the C arsenal takes as hooks. */
static void ss_q2_selected_observe(void *ctx, qa_actor_id actor, qa_q2w_owner *out_owner, qa_q2w_input *out_input) {
  qa_app_shared_simulation *sim = ((ss_q2_selected *)ctx)->sim;
  qa_ss_arsenal_observation player;
  if (!qa_app_shared_sim_arsenal_observation(sim, actor, &player)) ss_fail("Selected Q2 arsenal lost its player");
  out_owner->actor = player.actor; out_owner->view_height = player.view_height;
  *out_input = ss_source_is(sim, QA_SS_SOURCE_Q3_QVM) ? qa_app_shared_sim_qvm_q2_weapon_input(sim, actor)
      : ss_source_is(sim, QA_SS_SOURCE_Q2_NATIVE) ? qa_app_shared_sim_native_q2_weapon_input(sim, actor)
      : qa_app_shared_sim_q2_weapon_input(sim, qa_app_shared_sim_require_player(sim, actor));
}
static bool ss_q2_selected_is_live(void *ctx, qa_actor_id actor) { return qa_session_actor_registry_is_live(((ss_q2_selected *)ctx)->sim->actors, actor); }
static double ss_q2_selected_frame_seconds(void *ctx) { return ss_selected_q2(((ss_q2_selected *)ctx)->sim)->interval_milliseconds / 1000; }
static bool ss_q2_selected_combat_health(void *ctx, qa_actor_id actor, double *out) {
  qa_combat_state_result combat = qa_gameplay_authority_read(((ss_q2_selected *)ctx)->sim->combat, actor);
  if (!combat.has_value) return false;
  *out = combat.value.health; return true;
}
static void ss_q2_selected_released(void *ctx, qa_owned_actor actor) { qa_app_q2_selected_arsenal_release_actor(((ss_q2_selected *)ctx)->arsenal, actor.id); }
static void ss_dispose_q2_release(void *ctx) {
  ss_q2_selected *record = ctx;
  qa_session_actor_registry_off_release(record->sim->actors, record->release_subscription);
}
static void ss_q2_ammo_granted(void *ctx, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  ss_q2_selected *record = ctx;
  qa_app_q2_selected_arsenal_pickup_ammo(record->arsenal, actor, grants, count, auto_switch);
  qa_app_shared_sim_refresh_arsenal_cache(record->sim, actor.id);
}
static void ss_q2_weapon_granted(void *ctx, qa_owned_actor actor, const qa_item_id *weapons, size_t count, qa_pickup_selection selection) {
  ss_q2_selected *record = ctx;
  qa_app_q2_selected_arsenal_pickup_weapons(record->arsenal, actor, weapons, count, selection);
  qa_app_shared_sim_refresh_arsenal_cache(record->sim, actor.id);
}
static void ss_dispose_q2_weapons(void *ctx) { qa_q2w_weapons_destroy(ctx); }
static void ss_dispose_q2_mission(void *ctx) { qa_comp_selected_q2_mission_weapons_destroy(ctx); }

void qa_app_shared_sim_create_selected_q2_arsenal(qa_app_shared_simulation *sim, qa_ss_selected_arsenal *out) {
  char product[32] = "";
  { const char *content = sim->weapon_provider.content, *first = strchr(content, ':'), *second = first ? strchr(first + 1, ':') : NULL;
    if (second != NULL) { const char *end = strchr(second + 1, ':'); size_t length = end ? (size_t)(end - second - 1) : strlen(second + 1);
      if (length < sizeof product) { memcpy(product, second + 1, length); product[length] = '\0'; } } }
  static const char *const programs[] = {"baseq2", "xatrix", "rogue", "mg2"};
  int program = -1;
  for (int i = 0; i < 4; i++) if (strcmp(product, programs[i]) == 0) program = i;
  if (program < 0) ss_fail("Selected Q2 arsenal has an unsupported source program");
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider);
  qa_clock_profile profile = timing.clock;
  if (profile.kind != QA_CLOCK_Q2_CLASSIC && profile.kind != QA_CLOCK_Q2_RERELEASE) ss_fail("Selected Q2 arsenal requires its source clock");
  bool classic = profile.kind == QA_CLOCK_Q2_CLASSIC;
  const char *edition = classic ? "classic" : "rerelease";
  double interval = classic ? 100 : profile.frame_milliseconds;
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(sim->options.seed, classic ? "classic" : "q2-rerelease");
  if (random == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_random, random);
  double initial = sim->source_frame.time.kind == QA_SOURCE_TIME_MILLISECONDS ? sim->source_frame.time.value : sim->source_frame.time.value * 1000;
  qa_frame_context frame; memset(&frame, 0, sizeof frame);
  frame.frame = 0;
  frame.time = classic ? (qa_source_time){QA_SOURCE_TIME_SECONDS, initial / 1000} : (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, initial};
  frame.elapsed = classic ? (qa_source_time){QA_SOURCE_TIME_SECONDS, interval / 1000} : (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, interval};
  frame.phase = QA_FRAME_PHASE_FRAME_ENTRY;
  ss_q2_selected *record = qa_app_shared_sim_keep(sim, sizeof *record);
  record->sim = sim;
  record->weapon_hooks.sim = sim; record->weapon_hooks.source = sim->weapon_provider;
  record->weapon_hooks.host = qa_app_shared_sim_q2_actor_host(sim, sim->weapon_provider,
      ss_runtime(sim, timing.numeric, random, SS_CLOCK_SELECTED_Q2, NULL, interval / 1000), ss_q2_no_monster, NULL, NULL);
  qa_q2_game_options options = ss_q2_options(sim, sim->weapon_provider.provider, ss_q2_edition(edition), sim->recipe.map.entities.provider);
  options.source_damage_modifier = qa_app_shared_sim_damage_modifier_option(sim, &record->weapon_hooks.modifier);
  record->game = qa_q2_entity_services_create(&record->weapon_hooks.host, options, NULL, 0);
  if (record->game == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_q2_game, record->game);
  qa_q2_game_services *game = qa_q2_entity_services_game(record->game);
  qa_q2w_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = record; hooks.emit = ss_q2_selected_emit; hooks.noise = ss_q2_selected_noise; hooks.dodge = ss_hook_dodge;
  hooks.quad_multiplier = ss_hook_quad; hooks.source_damage_multiplier = ss_hook_damage;
  hooks.firing_interval = ss_q2_selected_interval;
  hooks.lag_is_history = false; /* lagCompensation: { kind: "current-world" } */
  hooks.ammo_changed = ss_hook_ammo_changed; hooks.can_target = ss_hook_can_target;
  record->weapons = calloc(1, sizeof *record->weapons);
  if (record->weapons == NULL || !qa_q2w_weapons_init(record->weapons, hooks)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->weapons); ss_own(sim, ss_dispose_q2_weapons, record->weapons);
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2w_ballistics_callbacks(&record->weapons->base));
  memset(&sim->selected_weapon_source, 0, sizeof sim->selected_weapon_source);
  sim->has_selected_weapon_source = true;
  sim->selected_weapon_source.kind = "q2"; sim->selected_weapon_source.q2_game = game; sim->selected_weapon_source.weapons = record->weapons;
  sim->selected_weapon_source.random = random; sim->selected_weapon_source.frame = frame;
  sim->selected_weapon_source.map_milliseconds = initial; sim->selected_weapon_source.interval_milliseconds = interval;
  sim->selected_weapon_source.next_milliseconds = initial + interval;
  record->mission = calloc(1, sizeof *record->mission);
  if (record->mission == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->mission);
  qa_q2mp_projectile_hooks mission_hooks; memset(&mission_hooks, 0, sizeof mission_hooks);
  mission_hooks.gravity_ctx = record; mission_hooks.gravity = ss_q2_mission_gravity;
  mission_hooks.ctx = record; mission_hooks.monster = ss_q2_mission_monster; mission_hooks.player_effect = ss_q2_mission_player_effect;
  if (!qa_comp_register_selected_q2_mission_weapons(record->mission, game, record->weapons, (qa_comp_selected_q2_program)program, mission_hooks))
    qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_q2_mission, record->mission);
  /* identity: the Q2 base weapons supplying themselves. */
  record->identity_ammo = calloc(qa_q2w_base_weapon_count + 1, sizeof *record->identity_ammo);
  record->identity_weapons = calloc(qa_q2w_base_weapon_count + 1, sizeof *record->identity_weapons);
  if (record->identity_ammo == NULL || record->identity_weapons == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->identity_ammo); ss_own(sim, ss_dispose_free, record->identity_weapons);
  qa_pickup_supply_profile identity; memset(&identity, 0, sizeof identity);
  identity.id = "composition:q2-base-q2-supply";
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++) {
    const qa_q2w_base_definition *weapon = &qa_q2w_base_weapons[i];
    if (weapon->ammo == NULL) continue;
    bool seen = false;
    for (size_t j = 0; j < identity.ammo_count && !seen; j++) seen = qa_ss_is(record->identity_ammo[j].source, weapon->ammo);
    if (!seen) record->identity_ammo[identity.ammo_count++] = (qa_pickup_supply_mapping){weapon->ammo, &weapon->ammo, 1};
  }
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++)
    record->identity_weapons[identity.weapon_count++] = (qa_pickup_supply_mapping){qa_q2w_base_weapons[i].item, &qa_q2w_base_weapons[i].item, 1};
  identity.ammo = record->identity_ammo; identity.weapons = record->identity_weapons;
  const char *q2_family = sim->strategy->supply_family(sim);
  bool q1_source = qa_ss_is(q2_family, "q1"), q2_source = qa_ss_is(q2_family, "q2");
  const qa_pickup_supply_profile *base = q1_source ? &QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE : q2_source ? &identity : &QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE;
  qa_composition_expansion_supply_kind expansions[2];
  size_t expansion_count = 0;
  if (!classic) { expansions[0] = QA_COMPOSITION_EXPANSION_Q2_XATRIX; expansions[1] = QA_COMPOSITION_EXPANSION_Q2_ROGUE; expansion_count = 2; }
  else if (program == QA_COMP_SELECTED_Q2_XATRIX) expansions[expansion_count++] = QA_COMPOSITION_EXPANSION_Q2_XATRIX;
  else if (program == QA_COMP_SELECTED_Q2_ROGUE) expansions[expansion_count++] = QA_COMPOSITION_EXPANSION_Q2_ROGUE;
  if (!qa_composition_expansion_supply(base, expansions, expansion_count, &record->expanded)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_owned_profile, &record->expanded);
  qa_pickup_supply_profile expanded = qa_composition_owned_profile_view(&record->expanded);
  if (!qa_composition_expansion_source_supply(&expanded, &record->profile)) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_owned_profile, &record->profile);
  record->view = qa_composition_owned_profile_view(&record->profile);
  qa_app_q2_selected_arsenal_options arsenal; memset(&arsenal, 0, sizeof arsenal);
  arsenal.game = game; arsenal.provider = sim->weapon_provider.provider; arsenal.edition = edition; arsenal.weapons = record->weapons;
  arsenal.inventory = qa_app_shared_sim_inventory_access(sim);
  record->definitions = calloc(record->mission->inventory_definition_count + 1, sizeof *record->definitions);
  if (record->definitions == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_free, record->definitions);
  for (size_t i = 0; i < record->mission->inventory_definition_count; i++)
    record->definitions[i] = (qa_app_q2_inventory_definition){record->mission->inventory_definitions[i].item, record->mission->inventory_definitions[i].capacity};
  arsenal.inventory_definitions = record->definitions; arsenal.inventory_definition_count = record->mission->inventory_definition_count;
  arsenal.pickup_order = record->mission->pickup_order; arsenal.pickup_order_count = record->mission->pickup_order_count;
  size_t replaced_count = 0;
  record->replaced = sim->strategy->owns_supply(sim) ? NULL : qa_ss_replaced_supply_items(&record->view, &replaced_count);
  if (record->replaced != NULL) ss_own(sim, ss_dispose_free, record->replaced);
  arsenal.replaced_items = record->replaced; arsenal.replaced_item_count = replaced_count;
  if (!q2_source) {
    qa_composition_supply_loadout loadout = q1_source ? qa_composition_q1_q2_supply_loadout() : qa_composition_q3_q2_supply_loadout();
    record->loadout = calloc(loadout.inventory_count + 1, sizeof *record->loadout);
    if (record->loadout == NULL) qa_range_error("Out of memory");
    ss_own(sim, ss_dispose_free, record->loadout);
    for (size_t i = 0; i < loadout.inventory_count; i++) record->loadout[i] = (qa_app_q2_loadout_entry){loadout.inventory[i].item, loadout.inventory[i].count};
    arsenal.has_loadout = true; arsenal.loadout_weapon = loadout.weapon;
    arsenal.loadout_inventory = record->loadout; arsenal.loadout_inventory_count = loadout.inventory_count;
  }
  arsenal.ctx = record; arsenal.observe = ss_q2_selected_observe; arsenal.is_live = ss_q2_selected_is_live;
  arsenal.frame_seconds = ss_q2_selected_frame_seconds; arsenal.combat_health = ss_q2_selected_combat_health;
  memset(out, 0, sizeof *out);
  out->family = "q2"; out->q2 = qa_app_q2_selected_arsenal_create(arsenal);
  if (out->q2 == NULL) qa_range_error("Out of memory");
  record->arsenal = out->q2;
  /* The TS constructor's options.game.host.actors.onRelease(actor => this.turns.delete(actor.id)). */
  record->release_subscription = qa_session_actor_registry_on_release(sim->actors, ss_q2_selected_released, record);
  ss_own(sim, ss_dispose_q2_release, record);
  qa_app_shared_sim_bind_selected_ammo(sim, &record->view);
  qa_shared_pickup_admission_options admission; memset(&admission, 0, sizeof admission);
  admission.inventory = sim->inventory; admission.profile = record->view; admission.ctx = record;
  admission.ammo_granted = ss_q2_ammo_granted; admission.weapon_granted = ss_q2_weapon_granted;
  sim->selected_supply = qa_shared_pickup_admission_create(admission);
  if (sim->selected_supply == NULL) qa_range_error("Out of memory");
  ss_own(sim, ss_dispose_admission, sim->selected_supply);
  record->admission = qa_shared_pickup_admission_as_contract(sim->selected_supply);
  sim->strategy->install_pickup_admission(sim, &record->admission);
  qa_app_shared_sim_prepare_selected_original_pickups(sim, &record->view);
}
