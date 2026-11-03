/* Port of src/app/bootstrap/simulation/foreign-deathball-rules.ts (DeathballRules): moved out of
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

/* ---- DeathBall (6297-6415) ---- */
/* this.scene.geometry.models[0]?.bounds.min.z */
static bool wl_world_floor(const qa_app_shared_simulation *sim, double *out) {
  const qa_decoded_world *world = &sim->world;
  if (world->kind == QA_DECODED_WORLD_Q1) { if (world->as.q1->model_count == 0) return false; *out = world->as.q1->models[0].bounds.min.z; return true; }
  if (world->kind == QA_DECODED_WORLD_Q2) { if (world->as.q2->model_count == 0) return false; *out = world->as.q2->models[0].bounds.min.z; return true; }
  if (world->as.q3->model_count == 0) return false;
  *out = world->as.q3->models[0].bounds.min.z;
  return true;
}
bool qa_app_shared_sim_foreign_deathball(qa_app_shared_simulation *sim) {
  return qa_ss_is(sim->recipe.match.provider, "q2:deathball") && qa_fm_deathmatch(sim) && qa_fm_family_is_not_q2_map(sim);
}
/* deathballOrigins(): the ball start (false = null) and the goals map. */
bool qa_app_shared_sim_deathball_origins(qa_app_shared_simulation *sim, qa_vec3 *ball_out) {
  if (!sim->has_deathball_spots) {
    bool has_ball = false; qa_vec3 ball = qa_ss_zero;
    size_t entity_count = 0;
    qa_q3_entity *entities = qa_q3_parse_entities(sim->world_entities, NULL, &entity_count);
    qa_vec3 *goals = qa_fm_alloc(entity_count, sizeof *goals); size_t goal_count = 0;
    bool has_team1 = false; qa_vec3 team1 = qa_ss_zero;
    for (size_t i = 0; i < entity_count; i++) {
      const char *classname = qa_q3_entity_get(&entities[i], "classname");
      if (!classname) classname = "";
      qa_vec3 origin;
      if (!qa_fm_entity_origin(&entities[i], &origin)) continue;
      if (qa_ss_is(classname, "dm_dball_ball_start") && !has_ball) { ball = origin; has_ball = true; }
      else if (qa_ss_is(classname, "dm_dball_goal")) goals[goal_count++] = origin;
      /* TS keeps both team starts; only team 1's decides the goal sides. */
      else if (qa_ss_is(classname, "dm_dball_team1_start") && !has_team1) { team1 = origin; has_team1 = true; }
    }
    qa_q3_entities_destroy(entities, entity_count);
    size_t spot_count; const qa_vec3 *spots = qa_fm_tag_spawn_origins(sim, &spot_count);
    qa_ss_map_clear(&sim->deathball_goals);
    double one = 1, two = 2;
    if (goal_count >= 2) {
      qa_vec3 first = goals[0], second = goals[1];
      bool first_is_team1 = !has_team1 ? first.x <= second.x
        : qa_js_hypot3(first.x - team1.x, first.y - team1.y, first.z - team1.z)
          <= qa_js_hypot3(second.x - team1.x, second.y - team1.y, second.z - team1.z);
      qa_ss_map_set(&sim->deathball_goals, &one, first_is_team1 ? &first : &second);
      qa_ss_map_set(&sim->deathball_goals, &two, first_is_team1 ? &second : &first);
    } else if (spot_count >= 2) {
      const qa_vec3 *a = &spots[0], *b = NULL; double best = -1;
      for (size_t i = 0; i < spot_count; i++) {
        double distance = qa_js_hypot3(spots[i].x - a->x, spots[i].y - a->y, spots[i].z - a->z);
        if (distance > best) { best = distance; b = &spots[i]; }
      }
      if (b) {
        const qa_vec3 *c = NULL; best = -1;
        for (size_t i = 0; i < spot_count; i++) {
          double distance = qa_js_hypot3(spots[i].x - b->x, spots[i].y - b->y, spots[i].z - b->z);
          if (distance > best) { best = distance; c = &spots[i]; }
        }
        if (c) { qa_ss_map_set(&sim->deathball_goals, &one, c); qa_ss_map_set(&sim->deathball_goals, &two, b); }
      }
    }
    free(goals);
    if (!has_ball) {
      /* A synthesized ball start must clear both synthesized goals and every player. */
      size_t player_count; qa_actor_id *players = qa_fm_players(sim, &player_count);
      qa_vec3 *occupied = qa_fm_alloc(player_count, sizeof *occupied); size_t occupied_count = 0;
      for (size_t i = 0; i < player_count; i++) {
        qa_body_state body;
        if (qa_app_shared_sim_body(sim, players[i], &body)) occupied[occupied_count++] = body.origin;
      }
      free(players);
      const qa_vec3 *best = NULL; double best_clearance = -1;
      for (size_t i = 0; i < spot_count; i++) {
        const qa_vec3 *spot = &spots[i];
        bool blocked = false;
        for (size_t j = 0; j < occupied_count; j++)
          if (qa_js_hypot2(spot->x - occupied[j].x, spot->y - occupied[j].y) < 64 && fabs(spot->z - occupied[j].z) < 64) blocked = true;
        if (blocked) continue;
        double clearance = INFINITY;
        for (size_t j = 0; j < sim->deathball_goals.count; j++) {
          const qa_vec3 *goal = qa_ss_map_value_at(&sim->deathball_goals, j);
          clearance = qa_js_min(clearance, qa_js_hypot3(spot->x - goal->x, spot->y - goal->y, spot->z - goal->z));
        }
        if (clearance > best_clearance) { best_clearance = clearance; best = spot; }
      }
      free(occupied);
      if (best) { ball = *best; has_ball = true; }
      else has_ball = qa_app_shared_sim_farthest_deathmatch_spawn(sim, &ball);
    }
    sim->has_deathball_spots = true; sim->has_deathball_ball = has_ball; sim->deathball_ball = ball;
  }
  if (sim->has_deathball_ball) *ball_out = sim->deathball_ball;
  return sim->has_deathball_ball;
}
void qa_app_shared_sim_spawn_deathball(qa_app_shared_simulation *sim, qa_vec3 origin) {
  qa_owned_actor actor = qa_session_actor_registry_allocate(sim->actors, sim->recipe.match.provider, "q2:dm_dball_ball");
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_body_state body = {{origin.x, origin.y, origin.z + 16}, qa_ss_zero, qa_ss_zero, bounds, NULL};
  qa_shared_body_table_create_body(qa_fm_bodies(sim), actor, body);
  qa_app_sim_shared_solid solid; memset(&solid, 0, sizeof solid);
  solid.solid = "box"; solid.family = qa_fm_family_name(sim->recipe.map.entities.provider);
  qa_app_sim_shared_physics_set_solid(sim->physics, actor, solid);
  qa_q2_motion motion; memset(&motion, 0, sizeof motion);
  motion.actor = actor; motion.kind = "toss"; motion.gravity = 1; motion.gravity_vector = (qa_vec3){0, 0, -1}; motion.clip_mask = 3;
  qa_app_sim_shared_physics_set_motion(sim->physics, actor, motion);
  qa_combat_state combat; memset(&combat, 0, sizeof combat);
  combat.health = 50000; combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat.armor.powered.kind = "none";
  combat.mass = 50; combat.can_take_damage = true;
  qa_gameplay_authority_create_state(sim->combat, actor, combat, NULL, NULL);
  qa_fm_detached_model(sim, actor, "models/objects/dball/tris.md2");
  qa_shared_body_table_link(qa_fm_bodies(sim), actor, NULL);
  sim->has_deathball = true; sim->deathball = actor;
}
void qa_app_shared_sim_prepare_deathball(qa_app_shared_simulation *sim) {
  sim->deathball_prepared = true;
  qa_vec3 ball;
  if (qa_app_shared_sim_deathball_origins(sim, &ball)) qa_app_shared_sim_spawn_deathball(sim, ball);
}
void qa_app_shared_sim_tick_deathball(qa_app_shared_simulation *sim) {
  if (!qa_app_shared_sim_foreign_deathball(sim)) return;
  if (!sim->deathball_prepared) qa_app_shared_sim_prepare_deathball(sim);
  size_t player_count; qa_actor_id *players = qa_fm_players(sim, &player_count);
  qa_fm_assign_teams(sim, &sim->deathball_teams, players, player_count);
  free(players);
  if (sim->has_deathball_reset_at && qa_fm_time(sim) >= sim->deathball_reset_at) {
    sim->has_deathball_reset_at = false;
    qa_vec3 home; bool has_home = qa_app_shared_sim_deathball_origins(sim, &home);
    qa_body_state body;
    if (sim->has_deathball && qa_app_shared_sim_body(sim, sim->deathball.id, &body) && has_home) {
      body.origin = (qa_vec3){home.x, home.y, home.z + 16}; body.velocity = qa_ss_zero;
      qa_shared_body_table_write(qa_fm_bodies(sim), sim->deathball, body);
      qa_shared_body_table_link(qa_fm_bodies(sim), sim->deathball, NULL);
    }
  }
  if (!sim->has_deathball || sim->has_deathball_reset_at) return;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, sim->deathball.id, &body)) return;
  double floor;
  if (wl_world_floor(sim, &floor) && body.origin.z < floor - 256) {
    sim->has_deathball_reset_at = true; sim->deathball_reset_at = qa_fm_time(sim);
    return;
  }
  qa_vec3 unused; qa_app_shared_sim_deathball_origins(sim, &unused);
  for (size_t i = 0; i < sim->deathball_goals.count; i++) {
    double team = qa_ss_map_key_at(&sim->deathball_goals, i)->number;
    const qa_vec3 *goal = qa_ss_map_value_at(&sim->deathball_goals, i);
    if (qa_js_hypot3(body.origin.x - goal->x, body.origin.y - goal->y, body.origin.z - goal->z) > 64) continue;
    /* Native DeathBall: the whole scoring team gains a point, then the ball
     * returns to its start after a two second pause. */
    double scoring = team == 1 ? 2 : 1;
    size_t member_count = sim->deathball_teams.count;
    qa_actor_id *members = qa_fm_alloc(member_count, sizeof *members); double *member_teams = qa_fm_alloc(member_count, sizeof *member_teams);
    for (size_t j = 0; j < member_count; j++) {
      members[j] = qa_ss_map_key_at(&sim->deathball_teams, j)->actor; member_teams[j] = *(double *)qa_ss_map_value_at(&sim->deathball_teams, j);
    }
    for (size_t j = 0; j < member_count; j++) if (member_teams[j] == scoring) qa_app_shared_sim_add_tag_score(sim, members[j], 1);
    free(members); free(member_teams);
    sim->has_deathball_reset_at = true; sim->deathball_reset_at = qa_fm_time(sim) + 2;
    return;
  }
}
