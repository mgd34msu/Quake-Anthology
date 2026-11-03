/* Port of src/app/bootstrap/simulation/foreign-match-host.ts (ForeignMatchHost helpers, ForeignMatchSpawns, balancedTeam): moved out of
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

/* The simulation helpers the rules share (shared_simulation_worlds.c keeps its own static copies for itself). */
noreturn void qa_fm_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
void *qa_fm_alloc(size_t count, size_t size) {
  void *memory = calloc(count ? count : 1, size);
  if (!memory) qa_range_error("Out of memory");
  return memory;
}
qa_shared_body_table *qa_fm_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
double qa_fm_time(const qa_app_shared_simulation *sim) { return qa_app_shared_sim_time_seconds(sim); }
/* players() copied: the phase A result is storage the next call reuses. */
qa_actor_id *qa_fm_players(qa_app_shared_simulation *sim, size_t *out_count) {
  size_t count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
  qa_actor_id *copy = qa_fm_alloc(count, sizeof *copy);
  if (count) memcpy(copy, players, count * sizeof *copy);
  *out_count = count;
  return copy;
}
bool qa_fm_includes(const qa_actor_id *actors, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++) if (qa_same_actor(actors[i], actor)) return true;
  return false;
}
/* this.combat.read(actor)?.health ?? 0 */
double qa_fm_health(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_combat_state_result state = qa_gameplay_authority_read(sim->combat, actor);
  return state.has_value ? state.value.health : 0;
}
const char *qa_fm_family_name(const char *provider) { return qa_game_family_name(qa_app_sim_provider_family(provider)); }
qa_q3_game_entity *qa_fm_q3_native(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
}

/* ---- Shared match helpers ---- */
bool qa_fm_family_is_not_q2_map(const qa_app_shared_simulation *sim) {
  return qa_app_sim_provider_family(sim->recipe.map.entities.provider) != QA_GAME_FAMILY_Q2;
}
bool qa_fm_deathmatch(const qa_app_shared_simulation *sim) { return qa_ss_is(sim->options.mode, "deathmatch"); }
qa_owned_actor qa_fm_spawn_trigger(qa_app_shared_simulation *sim, const char *definition, qa_vec3 origin, qa_bounds bounds,
    void (*touch)(void *ctx, qa_touch_contact contact)) {
  qa_owned_actor actor = qa_session_actor_registry_allocate(sim->actors, sim->recipe.match.provider, definition);
  qa_body_state body = {origin, qa_ss_zero, qa_ss_zero, bounds, NULL};
  qa_shared_body_table_create_body(qa_fm_bodies(sim), actor, body);
  qa_app_sim_shared_solid solid; memset(&solid, 0, sizeof solid);
  solid.solid = "trigger"; solid.family = qa_fm_family_name(sim->recipe.map.entities.provider);
  qa_app_sim_shared_physics_set_solid(sim->physics, actor, solid);
  qa_actor_callbacks callbacks = {sim, NULL, touch, NULL, NULL, NULL};
  qa_actor_callback_table_bind(sim->callbacks, actor, callbacks);
  return actor;
}
void qa_fm_detached_model(qa_app_shared_simulation *sim, qa_owned_actor actor, const char *path) {
  qa_ss_detached_model model; memset(&model, 0, sizeof model);
  model.content = sim->recipe.match.content; model.path = path;
  qa_ss_map_set(&sim->detached_models, &actor, &model);
}
/* Deathmatch spawn origins, flag spots and ball spots read "origin" as
 * split(/\s+/).map(Number) with exactly three finite parts. */
bool qa_fm_entity_origin(const qa_q3_entity *entity, qa_vec3 *out) {
  const char *text = qa_q3_entity_get(entity, "origin");
  if (!text) text = "";
  double parts[3]; size_t count = 0;
  const char *cursor = text;
  for (;;) {
    const char *start = cursor;
    while (*cursor && qa_js_white_space_length(cursor) == 0) cursor++;
    size_t length = (size_t)(cursor - start);
    char part[128];
    if (length >= sizeof part) return false;
    memcpy(part, start, length); part[length] = '\0';
    if (count == 3) return false;
    parts[count++] = qa_js_string_to_number(part);
    if (!*cursor) break;
    while (*cursor && qa_js_white_space_length(cursor) != 0) cursor += qa_js_white_space_length(cursor);
    /* A trailing separator still yields a final empty part (TS split). */
  }
  if (count != 3 || !isfinite(parts[0]) || !isfinite(parts[1]) || !isfinite(parts[2])) return false;
  *out = (qa_vec3){parts[0], parts[1], parts[2]};
  return true;
}

const qa_vec3 *qa_fm_tag_spawn_origins(qa_app_shared_simulation *sim, size_t *out_count) {
  if (!sim->has_tag_spawns) {
    size_t entity_count = 0;
    qa_q3_entity *entities = qa_q3_parse_entities(sim->world_entities, NULL, &entity_count);
    qa_vec3 *origins = qa_fm_alloc(entity_count, sizeof *origins), *fallback = qa_fm_alloc(entity_count, sizeof *fallback);
    size_t origin_count = 0, fallback_count = 0;
    for (size_t i = 0; i < entity_count; i++) {
      const char *classname = qa_q3_entity_get(&entities[i], "classname");
      if (!qa_ss_is(classname, "info_player_deathmatch") && !qa_ss_is(classname, "info_player_start")) continue;
      qa_vec3 origin;
      if (!qa_fm_entity_origin(&entities[i], &origin)) continue;
      if (qa_ss_is(classname, "info_player_deathmatch")) origins[origin_count++] = origin;
      else fallback[fallback_count++] = origin;
    }
    qa_q3_entities_destroy(entities, entity_count);
    if (origin_count != 0) { free(fallback); sim->tag_spawns = origins; sim->tag_spawn_count = origin_count; }
    else { free(origins); sim->tag_spawns = fallback; sim->tag_spawn_count = fallback_count; }
    sim->has_tag_spawns = true;
  }
  *out_count = sim->tag_spawn_count;
  return sim->tag_spawns;
}
const qa_vec3 *qa_app_shared_sim_tag_spawn_origins(qa_app_shared_simulation *sim, size_t *out_count) {
  return qa_fm_tag_spawn_origins(sim, out_count);
}
bool qa_app_shared_sim_farthest_deathmatch_spawn(qa_app_shared_simulation *sim, qa_vec3 *out) {
  size_t player_count; qa_actor_id *players = qa_fm_players(sim, &player_count);
  qa_vec3 *bodies = qa_fm_alloc(player_count, sizeof *bodies); size_t body_count = 0;
  for (size_t i = 0; i < player_count; i++) {
    qa_body_state body;
    if (qa_app_shared_sim_body(sim, players[i], &body)) bodies[body_count++] = body.origin;
  }
  free(players);
  size_t spot_count; const qa_vec3 *spots = qa_fm_tag_spawn_origins(sim, &spot_count);
  const qa_vec3 *best = NULL; double best_distance = -1;
  for (size_t i = 0; i < spot_count; i++) {
    double nearest = INFINITY;
    for (size_t j = 0; j < body_count; j++)
      nearest = qa_js_min(nearest, qa_js_hypot3(spots[i].x - bodies[j].x, spots[i].y - bodies[j].y, spots[i].z - bodies[j].z));
    if (nearest > best_distance) { best_distance = nearest; best = &spots[i]; }
  }
  free(bodies);
  if (!best) return false;
  *out = (qa_vec3){best->x, best->y, best->z + 24};
  return true;
}
/* Auto-balanced team assignment over a team map (LMCTF and DeathBall). */
void qa_fm_assign_teams(qa_app_shared_simulation *sim, qa_ss_map *teams, const qa_actor_id *players, size_t player_count) {
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id actor = players[i];
    if (qa_ss_map_has(teams, &actor)) continue;
    double red = 0, blue = 0;
    for (size_t j = 0; j < teams->count; j++) { if (*(double *)qa_ss_map_value_at(teams, j) == 1) red++; else blue++; }
    double team = blue < red ? 2 : 1;
    qa_ss_map_set(teams, &actor, &team);
    qa_owned_actor owned;
    if (qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) {
      qa_combat_trait_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_team = true; changes.team = team == 1 ? "red" : "blue";
      qa_gameplay_authority_set_traits(sim->combat, owned, changes);
    }
  }
}
