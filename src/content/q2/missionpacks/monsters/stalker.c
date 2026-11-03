/* Port of src/content/q2/missionpacks/monsters/stalker.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/stalker.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_stalker.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/foundation/js.h"

typedef struct stalker_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_weapons weapons;
  qa_q2mpm_monster_state *source;
  qa_q2mpm_gravity_fn gravity;
} stalker_bundle;
static bool on_ceiling(qa_q2m_context *c) { return c->entity->gravity_vector.z > 0; }
static bool trace_solid(qa_trace_result trace) {
  return strcmp(trace.kind, "q1") != 0 && (qa_js_to_uint32(trace.contents) & 1u) != 0;
}
static bool trace_world(qa_trace_result trace, qa_q2m_context *c) {
  return qa_q2mpm_source_trace_world(c->game, trace);
}
bool qa_q2mpm_stalker_transition_ok(qa_q2m_context *context, bool spawned_by_widow) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_body_state body = q2mpm_body(game, entity);
  bool ceiling = on_ceiling(context);
  double margin = ceiling ? body.bounds.min.z - 8 : body.bounds.max.z + 8;
  qa_vec3 end = body.origin;
  end.z += ceiling ? -384 : spawned_by_widow ? 256 : 180;
  qa_trace_result trace = q2mpm_trace(game, body.origin, end, &body.bounds, &entity->actor.id,
                                    qa_q2m_monster_solid_mask(game));
  if (trace.fraction == 1 || !trace_solid(trace) || !trace_world(trace, context)) {
    double normal_z = trace.contact.has_plane ? trace.contact.plane.normal.z : 0;
    if (ceiling ? normal_z < 0.9 : normal_z > -0.9) return false;
  }
  double end_height = trace.end.z + margin;
  const qa_vec3 corners[4] = {
    {body.bounds.min.x, body.bounds.min.y, 0}, {body.bounds.max.x, body.bounds.min.y, 0},
    {body.bounds.max.x, body.bounds.max.y, 0}, {body.bounds.min.x, body.bounds.max.y, 0},
  };
  for (int i = 0; i < 4; i++) {
    qa_vec3 start = {body.origin.x + corners[i].x - (corners[i].x < 0 ? 1 : -1),
                     body.origin.y + corners[i].y - (corners[i].y < 0 ? 1 : -1), body.origin.z};
    qa_vec3 stop = start;
    stop.z = end_height;
    qa_trace_result corner = q2mpm_trace(game, start, stop, NULL, &entity->actor.id, qa_q2m_monster_solid_mask(game));
    if (corner.fraction == 1 || !trace_solid(corner) || !trace_world(corner, context)
        || fabs(trunc(end_height - corner.end.z)) > 8) return false;
  }
  return true;
}
void qa_q2mpm_stalker_jump_angles(qa_vec3 start, qa_vec3 end, double velocity, double out[2]) {
  qa_vec3 delta = qa_sub3_double(end, start);
  double horizontal = hypot(delta.x, delta.y);
  double vertical = fabs(delta.z);
  double distance = hypot(horizontal, vertical);
  double angle = vertical == 0 ? 0 : atan(vertical / horizontal) * (delta.z > 0 ? -1 : 1);
  double first = asin(distance * 800 * qa_js_pow(cos(angle), 2) / qa_js_pow(velocity, 2) - sin(angle));
  out[0] = (first - angle) / 2;
  out[1] = (acos(-1.0) - first - angle) / 2;
}
static bool transition_ok(qa_q2m_context *c) {
  return qa_q2mpm_stalker_transition_ok(c, c->state->spawned_by == QA_Q2M_SPAWNED_WIDOW);
}
static void stand_move(qa_q2m_context *c) {
  q2mpm_set_move(c, q2mpm_rnd(c->game) < 0.25 ? "stalker_move_stand" : "stalker_move_idle2");
}
static void stand_fn(void *ctx, qa_q2m_context *c) { (void)ctx; stand_move(c); }
static void run_move(qa_q2m_context *c) {
  q2mpm_set_move(c, c->state->stand_ground ? "stalker_move_stand" : "stalker_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void detach(qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_body_state body = q2mpm_body(c->game, entity);
  double roll = body.angles.z + 180;
  entity->gravity_vector.z = -1;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true;
  change.angles = body.angles;
  change.angles.z = roll > 360 ? roll - 360 : roll;
  change.has_ground = true;
  change.ground_null = true;
  q2mpm_move_body(c->game, entity, change, false, false);
}
static void jump_straight_up(qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state body = q2mpm_body(game, entity);
  if (c->state->dead) return;
  if (on_ceiling(c)) {
    if (transition_ok(c)) detach(c);
    return;
  }
  if (body.ground == NULL) return;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  double x2 = body.velocity.x + q2mpm_rnd(game) * 10 - 5;
  double y2 = body.velocity.y + q2mpm_rnd(game) * 10 - 5;
  change.velocity = q2mpm_v3(x2,
                             y2,
                             body.velocity.z - 400 * entity->gravity_vector.z);
  q2mpm_move_body(game, entity, change, false, false);
  if (transition_ok(c)) {
    entity->gravity_vector.z = 1;
    memset(&change, 0, sizeof change);
    change.has_angles = true;
    change.angles = body.angles;
    change.angles.z = 180;
    change.has_ground = true;
    change.ground_null = true;
    q2mpm_move_body(game, entity, change, false, false);
  }
}
static bool pounce(stalker_bundle *bundle, qa_q2m_context *c, qa_vec3 destination) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  qa_body_state body = q2mpm_body(game, entity);
  if (on_ceiling(c) || !entity->has_enemy || enemy.ground == NULL
      || (qa_js_to_uint32(game->host->point_contents(game->host->ctx, destination)) & 56u) != 0) return false;
  qa_q2m_context *enemy_context = qa_q2m_monsters_context(bundle->monsters, entity->enemy);
  bool wet;
  if (enemy_context) {
    wet = enemy_context->state->water_level > 0;
  } else {
    qa_vec3 probe = enemy.origin;
    probe.z += enemy.bounds.min.z + 1;
    wet = (qa_js_to_uint32(game->host->point_contents(game->host->ctx, probe)) & 56u) != 0;
  }
  if (wet) return false;
  const qa_vec3 corners[4] = {
    {enemy.bounds.min.x, enemy.bounds.min.y, 0}, {enemy.bounds.max.x, enemy.bounds.min.y, 0},
    {enemy.bounds.max.x, enemy.bounds.max.y, 0}, {enemy.bounds.min.x, enemy.bounds.max.y, 0},
  };
  for (int i = 0; i < 4; i++) {
    qa_vec3 probe = {corners[i].x, corners[i].y, enemy.bounds.min.z - 0.25};
    if ((qa_js_to_uint32(game->host->point_contents(game->host->ctx, probe)) & 3u) == 0) return false;
  }
  qa_vec3 delta = qa_sub3_double(destination, body.origin);
  qa_vec3 angles = qa_vector_to_angles_with_rule(delta, QA_VECTOR_TO_ANGLES_NATIVE_AI);
  if (fabs(trunc(angles.y - body.angles.y)) > 45) return false;
  c->state->ideal_yaw = angles.y;
  qa_q2m_change_yaw(c);
  if (qa_hypot3(delta) > 450) return false;
  bool high = delta.z >= 32;
  qa_vec3 target = destination;
  target.z += high ? 32 : 0;
  qa_trace_result trace = q2mpm_trace(game, body.origin, destination, NULL, &entity->actor.id,
                                      qa_q2m_monster_solid_mask(game));
  if (trace.fraction < 1 && (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)))
    high = true;
  double velocity = 400.1;
  double jump[2] = {NAN, NAN};
  while (velocity <= 800) {
    qa_q2mpm_stalker_jump_angles(body.origin, target, velocity, jump);
    if (!isnan(jump[0]) || !isnan(jump[1])) break;
    velocity += 200;
  }
  double chosen = !high && !isnan(jump[0]) ? jump[0] : jump[1];
  if (isnan(chosen)) return false;
  qa_vec3 forward = qa_normalize3_double_hypot(
      qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  change.velocity = qa_scale3_double(forward, velocity * cos(chosen));
  change.velocity.z = velocity * sin(chosen) + 0.5 * bundle->gravity.fn(bundle->gravity.ctx) * 0.1;
  q2mpm_move_body(game, entity, change, false, false);
  return true;
}
static void shoot(stalker_bundle *bundle, qa_q2m_context *c) {
  if (!qa_q2bm_alive_enemy(c)) return;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  if (q2mpm_body(game, entity).ground != NULL && q2mpm_rnd(game) < 0.33) {
    if (qa_hypot3(qa_sub3_double(enemy.origin, q2mpm_body(game, entity).origin)) > 256 || q2mpm_rnd(game) < 0.5)
      (void)pounce(bundle, c, enemy.origin);
    else jump_straight_up(c);
  }
  qa_vec3 start = qa_q2m_project_flash_default(c, q2mpm_v3(24, 0, 6));
  qa_vec3 direction = qa_sub3_double(enemy.origin, start);
  qa_vec3 end = enemy.origin;
  if (q2mpm_rnd(game) < 0.2 + 0.1 * game->options.skill) {
    end = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, qa_hypot3(direction) / 1000));
    direction = qa_sub3_double(end, start);
  }
  qa_trace_result trace = q2mpm_trace(game, start, end, NULL, &entity->actor.id, qa_q2m_attack_trace_mask(game));
  if (trace_world(trace, c) || (trace.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(trace.hit.actor, entity->enemy))) {
    (void)bundle->weapons.fire_blaster2(bundle->weapons.ctx, entity, game, start, direction, 15, 800, 8);
    qa_q2rm_monster_flash(c, 144, start, direction);
  }
}
static void reactivate(qa_q2m_context *c) {
  c->state->stand_ground = false;
  q2mpm_set_move(c, "stalker_move_false_death_end");
}
static void jump_impulse(qa_q2m_context *c, double forward_speed, double up_speed) {
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  c->entity->timestamp = q2mpm_now(c->game);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  change.velocity = qa_add3_double(qa_add3_double(body.velocity, qa_scale3_double(axes.forward, forward_speed)),
                                   qa_scale3_double(axes.up, up_speed));
  q2mpm_move_body(c->game, c->entity, change, false, false);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if ((qa_js_to_uint32(c->entity->spawnflags) & 8u) != 0) {
    c->entity->gravity_vector.z = 1;
    qa_q2_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_angles = true;
    change.angles = q2mpm_body(c->game, c->entity).angles;
    change.angles.z = 180;
    q2mpm_move_body(c->game, c->entity, change, false, false);
  }
}
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, q2mpm_rnd(c->game) < 0.35 ? "stalker_move_idle" : "stalker_move_idle2");
}
static const qa_q2bm_sound_spec sight_spec = {"stalker/sight.wav", 1, 1};
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!qa_q2bm_alive_enemy(c)) return;
  if (q2mpm_rnd(c->game) > 1 - 0.5 / c->game->options.skill) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  } else {
    if (q2mpm_rnd(c->game) <= 0.5) c->state->lefty = !c->state->lefty;
    c->state->attack_state = QA_Q2M_ATTACK_SLIDING;
  }
  q2mpm_set_move(c, "stalker_move_shoot");
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2bm_alive_enemy(c))
    q2mpm_set_move(c, q2mpm_rnd(c->game) < 0.5 ? "stalker_move_swing_l" : "stalker_move_swing_r");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (state->dead) return;
  qa_q2bm_damaged_skin(c);
  const char *name = state->move->name;
  if (game->options.skill == 3 || q2mpm_body(game, entity).ground == NULL
      || strcmp(name, "stalker_move_false_death_end") == 0 || strcmp(name, "stalker_move_false_death_start") == 0)
    return;
  if (strcmp(name, "stalker_move_false_death") == 0) { reactivate(c); return; }
  double hp = qa_q2m_health(game, &entity->actor.id);
  if (hp > 0 && hp < entity->max_health / 4 && q2mpm_rnd(game) < 0.2 * game->options.skill
      && (!on_ceiling(c) || transition_ok(c))) {
    entity->gravity_vector = q2mpm_v3(0, 0, -1);
    qa_q2_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_angles = true;
    change.angles = q2mpm_body(game, entity).angles;
    change.angles.z = 0;
    q2mpm_move_body(game, entity, change, false, false);
    state->stand_ground = true;
    q2mpm_set_move(c, "stalker_move_false_death_start");
    return;
  }
  if (q2mpm_now(game) < state->pain_time) return;
  state->pain_time = q2mpm_now(game) + 3;
  if (reaction.damage > 10) {
    q2mpm_set_move(c, q2mpm_body(game, entity).ground != NULL && q2mpm_rnd(game) < 0.5
                          ? "stalker_move_jump_straightup" : "stalker_move_pain");
    q2mpm_snd(c, "stalker/pain.wav", 1);
  }
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  c->game->motion(c->game->ctx, c->entity, "toss");
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true;
  change.angles = q2mpm_body(c->game, c->entity).angles;
  change.angles.z = 0;
  q2mpm_move_body(c->game, c->entity, change, false, false);
  c->entity->gravity_vector = q2mpm_v3(0, 0, -1);
  qa_q2bm_begin_death_default(c, reaction, "stalker/death.wav", "stalker_move_death");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  stalker_bundle *bundle = ctx;
  (void)trace; (void)gravity;
  if (q2mpm_body(c->game, c->entity).ground == NULL
      || qa_q2m_health(c->game, &c->entity->actor.id) <= 0) return;
  if (!c->entity->has_enemy) {
    c->entity->has_enemy = true;
    c->entity->enemy = attacker;
    qa_q2m_monsters_found_target(bundle->monsters, c);
    return;
  }
  if (eta >= 0.1 && eta <= 5) q2mpm_set_move(c, "stalker_move_jump_straightup");
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  stalker_bundle *bundle = ctx;
  qa_body_state enemy;
  if (!qa_q2bm_alive_enemy(c) || !qa_q2m_enemy_body(c, &enemy)) return false;
  const bool rerelease = c->game->options.edition == QA_Q2_EDITION_RERELEASE;
  if (!rerelease && qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)) return true;
  if (on_ceiling(c)) {
    if (!transition_ok(c)) return false;
    detach(c);
    return true;
  }
  if (!rerelease && qa_q2m_visible_enemy(c)) { (void)pounce(bundle, c, enemy.origin); return true; }
  if (qa_q2rm_blocked_check_jump_default(c, distance, 256, 68) != QA_Q2RM_JUMP_NONE) {
    q2mpm_set_move(c, enemy.origin.z >= q2mpm_body(c->game, c->entity).origin.z ? "stalker_move_jump_up"
                                                                                  : "stalker_move_jump_down");
    return true;
  }
  if (qa_q2rm_blocked_check_platform(c, distance)) return true;
  /* m_rogue_stalker.cpp rerelease pounces on a blocked step only one time in ten. */
  if (rerelease && qa_q2m_visible_enemy(c) && q2mpm_rnd(c->game) < 0.1) {
    (void)pounce(bundle, c, enemy.origin);
    return true;
  }
  return false;
}
static void idle_noise_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2mpm_snd4(c, "stalker/idle.wav", 1, 0.5, 2); }
static void heal_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double skill = c->game->options.skill;
  double hp = qa_q2m_health(c->game, &c->entity->actor.id) + (skill == 2 ? 2 : skill == 3 ? 3 : 1);
  if (hp > c->entity->max_health / 2) c->entity->skin = 0;
  qa_gameplay_authority_set_health(c->game->host->combat, c->entity->actor, qa_js_min(hp, c->entity->max_health));
  if (hp >= c->entity->max_health) reactivate(c);
}
static void shoot_fn(void *ctx, qa_q2m_context *c) { shoot(ctx, c); }
static void shoot2_fn(void *ctx, qa_q2m_context *c) {
  if (q2mpm_rnd(c->game) < 0.4 + 0.1 * c->game->options.skill) shoot(ctx, c);
}
static void swing_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2mpm_v3(80, 0, 0),
                          5 + floor(q2mpm_rnd(c->game) * 5), 50))
    q2mpm_snd(c, c->entity->frame < qa_q2mpm_rogue_stalker_frames_value.attack08 ? "stalker/melee2.wav"
                                                                                   : "stalker/melee1.wav", 1);
}
static void jump_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_straight_up(c); }
static void jump_wait_fn(void *ctx, qa_q2m_context *c) {
  stalker_bundle *bundle = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (q2mpm_rnd(game) < 0.3 + 0.1 * game->options.skill && q2mpm_now(game) >= state->attack_finished) {
    state->attack_finished = q2mpm_now(game) + 0.3;
    shoot(bundle, c);
  }
  if (q2mpm_body(game, entity).ground == NULL) {
    entity->gravity = 1.3;
    state->next_frame = entity->frame;
    if (qa_q2rm_monster_jump_finished(c)) {
      entity->gravity = 1;
      state->next_frame = entity->frame + 1;
    }
  } else {
    entity->gravity = 1;
    state->next_frame = entity->frame + 1;
  }
}
static void impulse_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impulse(c, 200, 450); }
static void impulse_down_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impulse(c, 100, 300); }
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-28, -28, -18}, {28, 28, -4}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
qa_q2m_definition qa_q2mpm_create_stalker_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                     qa_q2mpm_monster_state *source, qa_q2mpm_gravity_fn gravity) {
  stalker_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating stalker definition");
  bundle->monsters = monsters;
  bundle->weapons = weapons;
  bundle->source = source;
  bundle->gravity = gravity;
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 15 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating stalker definition");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"stalker_stand", {NULL, stand_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_walk", {(void *)"stalker_move_walk", qa_q2bm_move_handler_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_run", {NULL, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_idle_noise", {NULL, idle_noise_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_false_death", {(void *)"stalker_move_false_death", qa_q2bm_move_handler_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_heal", {NULL, heal_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_shoot_attack", {bundle, shoot_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_shoot_attack2", {bundle, shoot2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_swing_attack", {NULL, swing_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_jump_straightup", {NULL, jump_up_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_jump_wait_land", {bundle, jump_wait_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_jump_up", {NULL, impulse_up_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_jump_down", {NULL, impulse_down_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"monster_done_dodge", {NULL, done_dodge_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"stalker_dead", {NULL, dead_fn}};
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_stalker";
  definition.kind = "stalker";
  definition.model = "models/monsters/stalker/tris.md2";
  definition.health = 250;
  definition.gib_health = -50;
  definition.mass = 250;
  definition.bounds.min = q2mpm_v3(-28, -28, -18);
  definition.bounds.max = q2mpm_v3(28, 28, 18);
  definition.scale = 1;
  definition.initial_move = "stalker_move_stand";
  definition.moves = qa_q2mpm_rogue_stalker_moves;
  definition.move_count = qa_q2mpm_rogue_stalker_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = n;
  definition.stand = (qa_q2m_handler){NULL, stand_fn};
  definition.walk = qa_q2bm_move_handler("stalker_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.idle = (qa_q2m_handler){NULL, idle_fn};
  definition.sight = qa_q2bm_sound_handler(&sight_spec);
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.dodge_ctx = bundle;
  definition.dodge = dodge_fn;
  definition.blocked_ctx = bundle;
  definition.blocked = blocked_fn;
  return definition;
}
