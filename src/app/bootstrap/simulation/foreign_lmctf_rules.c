/* Port of src/app/bootstrap/simulation/foreign-lmctf-rules.ts (FlagMatchRules): moved out of
 * shared_simulation_worlds.c as TS moved it out of runtime.ts (no behavior change). The rules run over the
 * simulation, which is the TS ForeignMatchHost; the shared helpers are in foreign_match_host.c
 * (foreign_match_host.h). GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/catalog/monsters.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/monsters/definitions.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/bots/navigation/rerelease_path.h"
#include "qa/content/catalog/timing.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/actors/body.h"
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/composition/q2_q3_items.h"
#include "foreign_match_host.h"

/* ---- LMCTF / flag CTF (6059-6294) ---- */
bool qa_app_shared_sim_foreign_lmctf(qa_app_shared_simulation *sim) {
  return qa_ss_is(sim->recipe.match.provider, "q2:lmctf") && qa_fm_deathmatch(sim) && qa_fm_family_is_not_q2_map(sim);
}
bool qa_app_shared_sim_foreign_ctf(qa_app_shared_simulation *sim) {
  return qa_ss_is(sim->recipe.match.provider, "q1:ctf") && qa_fm_deathmatch(sim) && (sim->strategy->as_q1(sim) == NULL);
}
/* get flagMode: "lmctf" | "ctf" | NULL. */
const char *qa_app_shared_sim_flag_mode(qa_app_shared_simulation *sim) {
  return qa_app_shared_sim_foreign_lmctf(sim) ? "lmctf" : qa_app_shared_sim_foreign_ctf(sim) ? "ctf" : NULL;
}
/* lmctfRuneHeld(actor): the rune kind or NULL. */
const char *qa_app_shared_sim_lmctf_rune_held(qa_app_shared_simulation *sim, qa_actor_id actor) {
  for (size_t i = 0; i < sim->lmctf_runes.count; i++) {
    const qa_ss_lmctf_rune *rune = qa_ss_map_value_at(&sim->lmctf_runes, i);
    if (rune->has_holder && qa_same_actor(rune->holder, actor)) return qa_ss_map_key_at(&sim->lmctf_runes, i)->string;
  }
  return NULL;
}
bool qa_app_shared_sim_lmctf_haste(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_app_shared_sim_foreign_lmctf(sim) && qa_ss_is(qa_app_shared_sim_lmctf_rune_held(sim, actor), "haste");
}
/* lmctfQ1HasteRules(): native LMCTF runs the weapon cycle twice per frame
 * under haste; the Q1 arsenal expresses that as halved delays. */
static double wl_haste_attack_delay(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay) {
  (void)game; /* TS (_game, player, delay): the rule ignores its game argument. */
  return qa_app_shared_sim_lmctf_haste(ctx, player->actor.id) ? delay / 2 : delay;
}
static double wl_haste_frame_delay(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay) {
  (void)game; /* TS (_game, player, delay) */
  return qa_app_shared_sim_lmctf_haste(ctx, player->actor.id) ? delay / 2 : delay;
}
void qa_app_shared_sim_register_lmctf_q1_haste_rules(qa_app_shared_simulation *sim, qa_q1_services *game) {
  /* The game keeps the rules pointer, so the record lives as long as the simulation. */
  qa_q1_weapon_rules *rules = qa_app_shared_sim_keep(sim, sizeof *rules);
  rules->id = "q2:lmctf/haste"; rules->ctx = sim;
  rules->attack_delay = wl_haste_attack_delay; rules->frame_delay = wl_haste_frame_delay;
  qa_q1_register_weapon_rules(game, rules);
}
static void wl_lmctf_origins(qa_app_shared_simulation *sim) {
  if (sim->has_lmctf_spots) return;
  size_t entity_count = 0;
  qa_q3_entity *entities = qa_q3_parse_entities(sim->world_entities, NULL, &entity_count);
  qa_vec3 *health = qa_fm_alloc(entity_count, sizeof *health); size_t health_count = 0;
  for (size_t i = 0; i < entity_count; i++) {
    const char *classname = qa_q3_entity_get(&entities[i], "classname");
    if (!classname) classname = "";
    qa_vec3 origin;
    if (!qa_fm_entity_origin(&entities[i], &origin)) continue;
    if ((qa_ss_is(classname, "info_flag_red") || qa_ss_is(classname, "info_flag_blue") || qa_ss_is(classname, "item_flag_team1")
          || qa_ss_is(classname, "item_flag_team2")) && !qa_ss_map_has(&sim->lmctf_spot_flags, &classname))
      qa_ss_map_set(&sim->lmctf_spot_flags, &classname, &origin);
    else if (strncmp(classname, "item_health", 11) == 0) health[health_count++] = origin;
  }
  qa_q3_entities_destroy(entities, entity_count);
  sim->lmctf_spot_health = health; sim->lmctf_spot_health_count = health_count;
  sim->has_lmctf_spots = true;
}
bool qa_app_shared_sim_lmctf_flag_home(qa_app_shared_simulation *sim, double team, qa_vec3 *out) {
  wl_lmctf_origins(sim);
  const char *name = qa_ss_is(qa_app_shared_sim_flag_mode(sim), "ctf") ? (team == 1 ? "item_flag_team1" : "item_flag_team2")
    : (team == 1 ? "info_flag_red" : "info_flag_blue");
  const qa_vec3 *authored = qa_ss_map_get(&sim->lmctf_spot_flags, &name);
  if (authored) { *out = *authored; return true; }
  /* Native LMCTF synthesizes bases from the deathmatch spawns farthest apart. */
  size_t count; const qa_vec3 *spots = qa_fm_tag_spawn_origins(sim, &count);
  if (count == 0) return false;
  qa_vec3 first = spots[0], farthest = first; double farthest_distance = -1;
  for (size_t i = 0; i < count; i++) {
    double distance = qa_js_hypot3(spots[i].x - first.x, spots[i].y - first.y, spots[i].z - first.z);
    if (distance > farthest_distance) { farthest_distance = distance; farthest = spots[i]; }
  }
  if (team == 1) { *out = farthest; return true; }
  qa_vec3 other = first; double other_distance = -1;
  for (size_t i = 0; i < count; i++) {
    double distance = qa_js_hypot3(spots[i].x - farthest.x, spots[i].y - farthest.y, spots[i].z - farthest.z);
    if (distance > other_distance) { other_distance = distance; other = spots[i]; }
  }
  *out = other;
  return true;
}
static void wl_lmctf_flag_touched(qa_app_shared_simulation *sim, double team, qa_touch_contact contact);
static void wl_flag_touched_red(void *ctx, qa_touch_contact contact) { wl_lmctf_flag_touched(ctx, 1, contact); }
static void wl_flag_touched_blue(void *ctx, qa_touch_contact contact) { wl_lmctf_flag_touched(ctx, 2, contact); }
void qa_app_shared_sim_spawn_lmctf_flag(qa_app_shared_simulation *sim, double team, qa_vec3 origin) {
  qa_ss_lmctf_flag *state = qa_ss_map_get(&sim->lmctf_flags, &team);
  if (!state) return;
  bool ctf = qa_ss_is(qa_app_shared_sim_flag_mode(sim), "ctf");
  const char *definition = ctf ? (team == 1 ? "q1:item_flag_team1" : "q1:item_flag_team2") : (team == 1 ? "q2:item_flag_team1" : "q2:item_flag_team2");
  qa_bounds bounds = ctf ? (qa_bounds){{-16, -16, 0}, {16, 16, 74}} : (qa_bounds){{-15, -15, -15}, {15, 15, 33}};
  qa_owned_actor actor = qa_fm_spawn_trigger(sim, definition, origin, bounds, team == 1 ? wl_flag_touched_red : wl_flag_touched_blue);
  qa_ss_detached_model model; memset(&model, 0, sizeof model);
  model.content = sim->recipe.match.content;
  if (ctf) { model.path = "progs/flag.mdl"; model.family = "q1"; model.has_skin = true; model.skin = team - 1; model.has_effects = true; model.effects = team == 1 ? 32 : 16; }
  else model.path = team == 1 ? "players/male/flag1.md2" : "players/male/flag2.md2";
  qa_ss_map_set(&sim->detached_models, &actor, &model);
  qa_shared_body_table_link(qa_fm_bodies(sim), actor, NULL);
  state = qa_ss_map_get(&sim->lmctf_flags, &team);
  state->has_actor = true; state->actor = actor;
}
void qa_app_shared_sim_lmctf_take_flag(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned) && qa_ss_is(qa_app_shared_sim_flag_mode(sim), "lmctf"))
    qa_shared_inventory_table_consume(sim->inventory, owned, "q2:flag", qa_shared_inventory_table_count(sim->inventory, actor, "q2:flag"));
}
static void wl_lmctf_flag_touched(qa_app_shared_simulation *sim, double team, qa_touch_contact contact) {
  qa_ss_lmctf_flag *state = qa_ss_map_get(&sim->lmctf_flags, &team);
  if (!state || !state->has_actor || !qa_same_actor(contact.self.id, state->actor.id)) return;
  qa_actor_id player = contact.other;
  if (qa_app_shared_sim_player(sim, player) == NULL || qa_fm_health(sim, player) <= 0) return;
  double *player_team = qa_ss_map_get(&sim->lmctf_teams, &player);
  if (!player_team) return;
  if (*player_team == team) {
    double other_team = team == 1 ? 2 : 1;
    qa_ss_lmctf_flag *carried = qa_ss_map_get(&sim->lmctf_flags, &other_team);
    if (carried && carried->has_carrier && qa_same_actor(carried->carrier, player)) {
      /* Capture: carrier scores 5, every teammate scores the 10-point team bonus. */
      qa_app_shared_sim_add_tag_score(sim, player, 5);
      size_t ally_count = sim->lmctf_teams.count;
      qa_actor_id *allies = qa_fm_alloc(ally_count, sizeof *allies); double *ally_teams = qa_fm_alloc(ally_count, sizeof *ally_teams);
      for (size_t i = 0; i < ally_count; i++) {
        allies[i] = qa_ss_map_key_at(&sim->lmctf_teams, i)->actor; ally_teams[i] = *(double *)qa_ss_map_value_at(&sim->lmctf_teams, i);
      }
      for (size_t i = 0; i < ally_count; i++) if (ally_teams[i] == team) qa_app_shared_sim_add_tag_score(sim, allies[i], 10);
      free(allies); free(ally_teams);
      qa_app_shared_sim_lmctf_take_flag(sim, player);
      carried = qa_ss_map_get(&sim->lmctf_flags, &other_team);
      carried->has_carrier = false; carried->has_dropped_at = false;
      qa_app_shared_sim_spawn_lmctf_flag(sim, other_team, carried->home);
    } else if (state->has_dropped_at) {
      /* Own dropped flag returns home on touch. */
      qa_owned_actor flag = state->actor;
      state->has_actor = false; state->has_dropped_at = false;
      qa_session_actor_registry_release(sim->actors, flag);
      state = qa_ss_map_get(&sim->lmctf_flags, &team);
      qa_app_shared_sim_spawn_lmctf_flag(sim, team, state->home);
      qa_app_shared_sim_add_tag_score(sim, player, 1);
    }
    return;
  }
  /* Enemy pickup. */
  qa_owned_actor flag = state->actor;
  qa_session_actor_registry_release(sim->actors, flag);
  state = qa_ss_map_get(&sim->lmctf_flags, &team);
  state->has_actor = false;
  state->has_carrier = true; state->carrier = player; state->has_dropped_at = false;
  qa_owned_actor owned;
  if (qa_session_actor_registry_resolve_owned(sim->actors, player, &owned) && qa_ss_is(qa_app_shared_sim_flag_mode(sim), "lmctf"))
    qa_shared_inventory_table_give(sim->inventory, owned, "q2:flag", 1);
}
void qa_app_shared_sim_lmctf_flag_touched(qa_app_shared_simulation *sim, double team, qa_touch_contact contact) {
  wl_lmctf_flag_touched(sim, team, contact);
}
static const char *const wl_rune_kinds[] = {"damage", "haste", "resist", "regen", "vampire"};
static void wl_lmctf_rune_touched(qa_app_shared_simulation *sim, const char *kind, qa_touch_contact contact);
static void wl_rune_touched_0(void *ctx, qa_touch_contact contact) { wl_lmctf_rune_touched(ctx, wl_rune_kinds[0], contact); }
static void wl_rune_touched_1(void *ctx, qa_touch_contact contact) { wl_lmctf_rune_touched(ctx, wl_rune_kinds[1], contact); }
static void wl_rune_touched_2(void *ctx, qa_touch_contact contact) { wl_lmctf_rune_touched(ctx, wl_rune_kinds[2], contact); }
static void wl_rune_touched_3(void *ctx, qa_touch_contact contact) { wl_lmctf_rune_touched(ctx, wl_rune_kinds[3], contact); }
static void wl_rune_touched_4(void *ctx, qa_touch_contact contact) { wl_lmctf_rune_touched(ctx, wl_rune_kinds[4], contact); }
static void (*const wl_rune_touch[])(void *ctx, qa_touch_contact contact) = {wl_rune_touched_0, wl_rune_touched_1, wl_rune_touched_2,
  wl_rune_touched_3, wl_rune_touched_4};
static size_t wl_rune_index(const char *kind) {
  for (size_t i = 0; i < sizeof wl_rune_kinds / sizeof *wl_rune_kinds; i++) if (qa_ss_is(kind, wl_rune_kinds[i])) return i;
  qa_fm_fail("LMCTF rune kind %s is not declared", kind);
}
static const char *wl_rune_item(qa_app_shared_simulation *sim, const char *kind) {
  char item[64]; snprintf(item, sizeof item, "q2:%s_rune", kind);
  return qa_app_shared_sim_intern(sim, item);
}
void qa_app_shared_sim_spawn_lmctf_rune(qa_app_shared_simulation *sim, const char *kind, qa_vec3 origin) {
  qa_ss_lmctf_rune *state = qa_ss_map_get(&sim->lmctf_runes, &kind);
  if (!state) return;
  static const char *const models[] = {"models/ctf/damage/tris.md2", "models/ctf/haste/tris.md2", "models/ctf/resist/tris.md2",
    "models/ctf/regen/tris.md2", "models/ctf/resist/tris.md2"};
  size_t index = wl_rune_index(kind);
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_owned_actor actor = qa_fm_spawn_trigger(sim, wl_rune_item(sim, kind), (qa_vec3){origin.x, origin.y, origin.z + 16}, bounds, wl_rune_touch[index]);
  qa_fm_detached_model(sim, actor, models[index]);
  qa_shared_body_table_link(qa_fm_bodies(sim), actor, NULL);
  state = qa_ss_map_get(&sim->lmctf_runes, &kind);
  state->has_actor = true; state->actor = actor;
}
static void wl_lmctf_rune_touched(qa_app_shared_simulation *sim, const char *kind, qa_touch_contact contact) {
  qa_ss_lmctf_rune *state = qa_ss_map_get(&sim->lmctf_runes, &kind);
  if (!state || !state->has_actor || !qa_same_actor(contact.self.id, state->actor.id)) return;
  qa_actor_id player = contact.other;
  if (qa_app_shared_sim_player(sim, player) == NULL || qa_fm_health(sim, player) <= 0 || qa_app_shared_sim_lmctf_rune_held(sim, player) != NULL) return;
  qa_owned_actor rune = state->actor;
  qa_session_actor_registry_release(sim->actors, rune);
  state = qa_ss_map_get(&sim->lmctf_runes, &kind);
  state->has_actor = false; state->has_holder = true; state->holder = player;
  qa_owned_actor owned;
  if (qa_session_actor_registry_resolve_owned(sim->actors, player, &owned)) qa_shared_inventory_table_give(sim->inventory, owned, wl_rune_item(sim, kind), 1);
}
void qa_app_shared_sim_lmctf_rune_touched(qa_app_shared_simulation *sim, const char *kind, qa_touch_contact contact) {
  wl_lmctf_rune_touched(sim, kind, contact);
}
void qa_app_shared_sim_prepare_lmctf(qa_app_shared_simulation *sim) {
  sim->lmctf_prepared = true;
  for (double team = 1; team <= 2; team++) {
    qa_vec3 home;
    if (qa_app_shared_sim_lmctf_flag_home(sim, team, &home)) {
      qa_ss_lmctf_flag flag; memset(&flag, 0, sizeof flag); flag.home = home;
      qa_ss_map_set(&sim->lmctf_flags, &team, &flag);
      qa_app_shared_sim_spawn_lmctf_flag(sim, team, home);
    }
  }
  if (!qa_ss_is(qa_app_shared_sim_flag_mode(sim), "lmctf")) return;
  /* Runes scatter over health spots by farthest-point sampling; deathmatch spawns are the fallback. */
  wl_lmctf_origins(sim);
  size_t pool_count; const qa_vec3 *source_pool;
  if (sim->lmctf_spot_health_count != 0) { source_pool = sim->lmctf_spot_health; pool_count = sim->lmctf_spot_health_count; }
  else source_pool = qa_fm_tag_spawn_origins(sim, &pool_count);
  qa_vec3 *pool = qa_fm_alloc(pool_count, sizeof *pool);
  if (pool_count) memcpy(pool, source_pool, pool_count * sizeof *pool);
  qa_vec3 chosen[5]; size_t chosen_count = 0;
  for (size_t k = 0; k < 5; k++) {
    const char *kind = wl_rune_kinds[k];
    qa_ss_lmctf_rune rune; memset(&rune, 0, sizeof rune);
    qa_ss_map_set(&sim->lmctf_runes, &kind, &rune);
    const qa_vec3 *best = NULL; double best_distance = -1;
    for (size_t i = 0; i < pool_count; i++) {
      const qa_vec3 *spot = &pool[i];
      bool taken = false;
      for (size_t j = 0; j < chosen_count; j++) if (chosen[j].x == spot->x && chosen[j].y == spot->y && chosen[j].z == spot->z) taken = true;
      if (taken) continue;
      double nearest = INFINITY;
      for (size_t j = 0; j < chosen_count; j++)
        nearest = qa_js_min(nearest, qa_js_hypot3(spot->x - chosen[j].x, spot->y - chosen[j].y, spot->z - chosen[j].z));
      if (nearest > best_distance) { best_distance = nearest; best = spot; }
    }
    if (best) { chosen[chosen_count++] = *best; qa_app_shared_sim_spawn_lmctf_rune(sim, kind, *best); }
  }
  free(pool);
}
void qa_app_shared_sim_tick_lmctf(qa_app_shared_simulation *sim) {
  if (qa_app_shared_sim_flag_mode(sim) == NULL) return;
  if (!sim->lmctf_prepared) qa_app_shared_sim_prepare_lmctf(sim);
  size_t player_count; qa_actor_id *players = qa_fm_players(sim, &player_count);
  qa_fm_assign_teams(sim, &sim->lmctf_teams, players, player_count);
  for (size_t i = 0; i < sim->lmctf_flags.count; i++) {
    double team = qa_ss_map_key_at(&sim->lmctf_flags, i)->number;
    qa_ss_lmctf_flag *flag = qa_ss_map_value_at(&sim->lmctf_flags, i);
    if (flag->has_dropped_at && flag->has_actor && qa_fm_time(sim) - flag->dropped_at >= 30) {
      qa_owned_actor actor = flag->actor;
      flag->has_actor = false; flag->has_dropped_at = false;
      qa_session_actor_registry_release(sim->actors, actor);
      flag = qa_ss_map_value_at(&sim->lmctf_flags, i);
      qa_app_shared_sim_spawn_lmctf_flag(sim, team, flag->home);
      flag = qa_ss_map_value_at(&sim->lmctf_flags, i);
    }
    if (flag->has_carrier && !qa_fm_includes(players, player_count, flag->carrier)) {
      qa_actor_id carrier = flag->carrier;
      qa_app_shared_sim_lmctf_take_flag(sim, carrier);
      flag = qa_ss_map_value_at(&sim->lmctf_flags, i);
      flag->has_carrier = false;
      qa_app_shared_sim_spawn_lmctf_flag(sim, team, flag->home);
    }
  }
  for (size_t i = 0; i < sim->lmctf_runes.count; i++) {
    const char *kind = qa_ss_map_key_at(&sim->lmctf_runes, i)->string;
    qa_ss_lmctf_rune *rune = qa_ss_map_value_at(&sim->lmctf_runes, i);
    if (rune->has_holder && !qa_fm_includes(players, player_count, rune->holder)) {
      qa_owned_actor owned;
      if (qa_session_actor_registry_resolve_owned(sim->actors, rune->holder, &owned))
        qa_shared_inventory_table_consume(sim->inventory, owned, wl_rune_item(sim, kind), 1);
      rune = qa_ss_map_value_at(&sim->lmctf_runes, i);
      rune->has_holder = false;
      qa_vec3 spot;
      if (qa_app_shared_sim_farthest_deathmatch_spawn(sim, &spot)) qa_app_shared_sim_spawn_lmctf_rune(sim, kind, spot);
    }
  }
  for (size_t p = 0; p < player_count; p++) {
    qa_actor_id actor = players[p];
    /* Regen rune: native heart-rate cadence heals toward max health + 25. */
    if (qa_ss_is(qa_app_shared_sim_lmctf_rune_held(sim, actor), "regen")) {
      qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor); qa_owned_actor owned;
      if (combat.has_value && qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned) && combat.value.health > 0) {
        double heart_rate = qa_js_min(25, qa_js_max(5, trunc(combat.value.health / 5))), frame = qa_js_round(qa_fm_time(sim) * 10);
        double *regen_at = qa_ss_map_get(&sim->lmctf_regen_at, &actor);
        if (frame >= (regen_at ? *regen_at : 0) + heart_rate) {
          qa_ss_map_set(&sim->lmctf_regen_at, &actor, &frame);
          if (combat.value.health < 125)
            qa_gameplay_authority_set_health(sim->combat, owned, qa_js_min(125, trunc(combat.value.health + (float)(heart_rate / 3))));
        }
      }
    }
    double health = qa_fm_health(sim, actor);
    double *previous_value = qa_ss_map_get(&sim->lmctf_health, &actor);
    bool has_previous = previous_value != NULL; double previous = has_previous ? *previous_value : 0;
    qa_ss_map_set(&sim->lmctf_health, &actor, &health);
    if (!has_previous || previous <= 0 || health > 0) continue;
    /* Death drops the carried flag and held rune at the corpse. */
    qa_body_state body; bool has_origin = qa_app_shared_sim_body(sim, actor, &body);
    for (size_t i = 0; i < sim->lmctf_flags.count; i++) {
      double team = qa_ss_map_key_at(&sim->lmctf_flags, i)->number;
      qa_ss_lmctf_flag *flag = qa_ss_map_value_at(&sim->lmctf_flags, i);
      if (!flag->has_carrier || !qa_same_actor(flag->carrier, actor)) continue;
      qa_app_shared_sim_lmctf_take_flag(sim, actor);
      flag = qa_ss_map_value_at(&sim->lmctf_flags, i);
      flag->has_carrier = false; flag->has_dropped_at = true; flag->dropped_at = qa_fm_time(sim);
      qa_app_shared_sim_spawn_lmctf_flag(sim, team, has_origin ? body.origin : flag->home);
    }
    for (size_t i = 0; i < sim->lmctf_runes.count; i++) {
      const char *kind = qa_ss_map_key_at(&sim->lmctf_runes, i)->string;
      qa_ss_lmctf_rune *rune = qa_ss_map_value_at(&sim->lmctf_runes, i);
      if (!rune->has_holder || !qa_same_actor(rune->holder, actor)) continue;
      qa_owned_actor owned;
      if (qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned))
        qa_shared_inventory_table_consume(sim->inventory, owned, wl_rune_item(sim, kind), 1);
      rune = qa_ss_map_value_at(&sim->lmctf_runes, i);
      rune->has_holder = false;
      if (has_origin) qa_app_shared_sim_spawn_lmctf_rune(sim, kind, body.origin);
    }
  }
  free(players);
}
