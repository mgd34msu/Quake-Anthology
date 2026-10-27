/* Port of src/content/q1/missionpacks/monsters/armagon.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/armagon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hiparma.h"
#include "monsters_internal.h"

static qa_q1mpm_actor *body_part(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_actor *body =
      monster->game->entity(monster->game, qa_q1mpm_references_get(monster->entity, "trigger_field"));
  if (body == NULL) qa_range_error("Armagon lost its source body entity");
  return body;
}
static void sound(qa_q1mpm_mission_monster *monster, const char *path, qa_q1mpm_channel channel,
                  double volume, double attenuation) {
  qa_q1mpm_event event = {0};
  event.kind = "sound";
  event.actor = QA_Q1MPM_SOME(monster->entity->actor.id);
  event.path = path;
  event.channel = channel;
  event.volume = volume;
  event.attenuation = attenuation;
  monster->game->host->emit(monster->game->host->ctx, &event);
}
static qa_vec3 enemy_origin(qa_q1mpm_mission_monster *monster) {
  qa_vec3 target;
  return qa_q1mpm_monster_target(monster, &target) ? target : QA_Q1MPM_ZERO;
}
static qa_vec3 enemy_eye(qa_q1mpm_mission_monster *monster) {
  if (!monster->state.enemy.has) return QA_Q1MPM_ZERO;
  qa_vec3 value;
  return qa_q1mpm_eye(monster->game, monster->state.enemy.id, &value) ? value : QA_Q1MPM_ZERO;
}
static void turn(qa_q1mpm_mission_monster *monster, double difference, double target) {
  double current = qa_q1mpm_actor_number(monster->entity, "fixangle");
  qa_q1mpm_number(monster, "fixangle",
                  fabs(difference) < 10 ? target : difference > 5 ? current + 9
                                              : difference < -5   ? current - 9
                                                                  : target);
}
static qa_q1mpm_actor *sync_body(qa_q1mpm_mission_monster *monster, bool walking) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *body = body_part(monster);
  qa_vec3 angles = game->body(game, entity).angles;
  game->set_origin(game, body, qa_q1mpm_monster_origin(monster));
  if (walking && qa_q1mpm_str_eq(game->options.edition, "rerelease")) body->movement = "step";
  body->frame = entity->frame;
  qa_q1mpm_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = angles;
  patch.angles.y = angles.y + qa_q1mpm_actor_number(entity, "fixangle");
  game->set_body(game, body, &patch);
  return body;
}
static void idle_sound(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (game->health(game, entity->actor.id) < 0 || game->time <= qa_q1mpm_actor_number(entity, "super_time"))
    return;
  qa_q1mpm_number(monster, "super_time", game->time + 3);
  if (game->host->random(game->host->ctx) < 0.5) {
    double r = game->host->random(game->host->ctx);
    char path[32];
    snprintf(path, sizeof(path), "armagon/idle%d.wav", r < 0.25 ? 1 : r < 0.5 ? 2 : r < 0.75 ? 3 : 4);
    sound(monster, path, QA_Q1MPM_CH("voice"), 1, 0.5);
  }
}
static void think(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *body = sync_body(monster, false);
  entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(enemy_origin(monster), qa_q1mpm_monster_origin(monster)));
  double delta = entity->ideal_yaw - game->body(game, entity).angles.y;
  qa_q1mpm_number(monster, "cnt", 0);
  if (delta > 180) delta -= 360;
  if (delta < -180) delta += 360;
  if (fabs(delta) > 90) { delta = 0; qa_q1mpm_number(monster, "cnt", 1); }
  turn(monster, delta - qa_q1mpm_actor_number(entity, "fixangle"), delta);
  if (game->health(game, entity->actor.id) < 0) return;
  idle_sound(monster);
  if (qa_q1mpm_str_eq(game->options.edition, "rerelease") &&
      qa_q1mpm_length(qa_q1mpm_vsub(qa_q1mpm_actor_vector(body, "oldorigin"), game->body(game, body).origin)) > 50)
    body->movement = "step";
}
static void think_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; think(monster); }
static void walk_think_impl(qa_q1mpm_mission_monster *monster) {
  sync_body(monster, true);
  qa_q1mpm_monster_change_yaw(monster);
  qa_q1mpm_number(monster, "cnt", 0);
  turn(monster, -qa_q1mpm_actor_number(monster->entity, "fixangle"), 0);
  if (monster->game->health(monster->game, monster->entity->actor.id) < 0) return;
  idle_sound(monster);
  qa_q1mpm_opt_actor client =
      monster->game->host->check_client(monster->game->host->ctx, monster->entity->actor);
  if (client.has && qa_q1mpm_monster_visible_actor(monster, client.id))
    qa_q1mpm_monster_found(monster, client.id);
}
static void walk_think(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; walk_think_impl(monster); }
static void launch(qa_q1mpm_mission_monster *monster, double offset, int turn_direction, bool laser) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 angles = game->body(game, entity).angles;
  angles.y = angles.y + qa_q1mpm_actor_number(entity, "fixangle") +
             (turn_direction == 1 ? 165 : turn_direction == 2 ? -165 : 0);
  qa_q1mpm_basis basis = game->make_vectors(game, angles);
  qa_vec3 origin = qa_q1mpm_vadd(
      qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 66)),
                    qa_q1mpm_vscale(basis.right, offset)),
      qa_q1mpm_vscale(basis.forward, 84));
  qa_vec3 target = enemy_eye(monster);
  if (game->options.skill != 0) {
    qa_q1mpm_body enemy_body;
    qa_vec3 velocity = monster->state.enemy.has &&
                               game->host->bodies_read(game->host->ctx, monster->state.enemy.id, &enemy_body)
                           ? enemy_body.velocity
                           : QA_Q1MPM_ZERO;
    target = qa_q1mpm_vadd(target, qa_q1mpm_vscale(velocity, qa_q1mpm_length(qa_q1mpm_vsub(target, origin)) / 1000));
  }
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(target, origin));
  if (qa_q1mpm_dot(direction, basis.forward) < qa_q1mpm_actor_number(entity, "worldtype"))
    direction = basis.forward;
  entity->effects |= 2;
  if (laser) {
    qa_q1mpm_ext_launch_hipnotic_laser(game, entity->actor.id, origin, direction, false);
  } else {
    game->sound(game, entity, "weapons/sgun1.wav", QA_Q1MPM_CH("weapon"), 1, 1);
    qa_vec3 punch = qa_q1mpm_actor_vector(entity, "punchangle");
    char buf[96];
    char fy[32], fz[32];
    qa_q1mpm_format_number(fy, sizeof(fy), punch.y);
    qa_q1mpm_format_number(fz, sizeof(fz), punch.z);
    snprintf(buf, sizeof(buf), "-2 %s %s", fy, fz);
    qa_q1mpm_fields_set(entity, "punchangle", buf);
    qa_q1mpm_actor *shot = qa_q1mpm_ext_create_missile(game, QA_Q1MPM_SOME(entity->actor.id), "missile",
                                                       "missile", origin, qa_q1mpm_vscale(direction, 1000));
    shot->projectile = "rocket";
    shot->touch = game->named.touch(game->named.ctx, shot, "projectile_touch");
  }
}
static void over_think(qa_q1mpm_mission_monster *monster, bool left) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_monster_change_yaw(monster);
  game->host->walk_move(game->host->ctx, entity->actor, game->body(game, entity).angles.y, 14);
  sync_body(monster, false);
  double delta = 0;
  if (entity->count == 0) {
    entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(enemy_origin(monster), qa_q1mpm_monster_origin(monster)));
    delta = entity->ideal_yaw - game->body(game, entity).angles.y + (left ? -165 : 165);
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
  } else if (entity->count == 1) {
    launch(monster, left ? 40 : -40, left ? 1 : 2, false);
    return;
  }
  turn(monster, delta - qa_q1mpm_actor_number(entity, "fixangle"), delta);
}
static void over_left(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; over_think(monster, true); }
static void over_right(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; over_think(monster, false); }
static void walk(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *found[1];
  size_t count = game->find(game, monster->state.path, found, 1);
  qa_q1mpm_opt_actor goal = count > 0 ? QA_Q1MPM_SOME(found[0]->actor.id)
                                      : game->world == NULL ? QA_Q1MPM_NONE
                                                            : QA_Q1MPM_SOME(game->world->actor.id);
  if (goal.has) game->host->move_to_goal(game->host->ctx, entity->actor, goal.id, 14, NULL);
}
static void run_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_actor *world = monster->game->world;
  if (world == NULL) qa_range_error("Armagon requires a world entity");
  qa_q1mpm_monster_change_yaw(monster);
  qa_q1mpm_fields_set(world, "RUN_STRAIGHT", "1");
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_RUN, 14);
  think(monster);
}
static void run_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; run_impl(monster); }
static void walking_attack_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_monster_change_yaw(monster);
  monster->game->host->walk_move(monster->game->host->ctx, monster->entity->actor,
                                  monster->game->body(monster->game, monster->entity).angles.y, 14);
  think(monster);
}
static void repulse(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  think(monster);
  if (qa_q1mpm_actor_number(entity, "state") == 0) {
    qa_q1mpm_monster_attack_finished(monster, 0.5);
    game->sound(game, entity, "armagon/repel.wav", QA_Q1MPM_CH("body"), 1, 1);
    qa_q1mpm_number(monster, "state", 1);
    return;
  }
  if (qa_q1mpm_actor_number(entity, "state") != 1) return;
  qa_actor_id actors[256];
  size_t count = qa_q1mpm_radius_actors(game, qa_q1mpm_monster_origin(monster), 300, actors, 256);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    if (!game->is_player(game, QA_Q1MPM_SOME(actor))) continue;
    qa_q1mpm_actor *other = game->entity(game, QA_Q1MPM_SOME(actor));
    if (((other == NULL ? 0 : other->movement_flags) & 128) != 0) continue;
    if (!qa_q1mpm_monster_visible_actor(monster, actor) || game->health(game, actor) <= 0) continue;
    qa_owned_actor owner;
    qa_q1mpm_body body;
    if (!game->host->actors_resolve_owned(game->host->ctx, actor, &owner) ||
        !game->host->bodies_read(game->host->ctx, actor, &body))
      continue;
    qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(
        body.origin, qa_q1mpm_vsub(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 24))));
    body.velocity = qa_q1mpm_vadd(body.velocity, qa_q1mpm_vscale(direction, 1500));
    game->host->bodies_write(game->host->ctx, owner, &body);
  }
  game->radius_damage(game, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 60,
                      QA_Q1MPM_SOME(entity->actor.id), NULL, "");
  qa_q1mpm_number(monster, "state", 0);
  qa_q1mpm_monster_attack_finished(monster, 0.1);
}
typedef struct clear_shot_result { bool clear; bool crossed_water; double distance; } clear_shot_result;
static clear_shot_result clear_shot(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 start;
  if (!qa_q1mpm_monster_eye(monster, &start)) start = qa_q1mpm_monster_origin(monster);
  qa_vec3 end = enemy_eye(monster);
  qa_q1mpm_opt_actor enemy = monster->state.enemy.has
                                 ? monster->state.enemy
                                 : game->world == NULL ? QA_Q1MPM_NONE
                                                       : QA_Q1MPM_SOME(game->world->actor.id);
  qa_q1mpm_trace_request request = {start, end, QA_Q1MPM_POINT, QA_Q1MPM_SOME(entity->actor.id), true, false};
  qa_q1mpm_trace trace = game->host->trace(game->host->ctx, &request);
  clear_shot_result result = {enemy.has && trace.actor.has && qa_same_actor(trace.actor.id, enemy.id),
                              trace.in_open && trace.in_water,
                              qa_q1mpm_length(qa_q1mpm_vsub(end, start))};
  return result;
}
static void stand_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  clear_shot_result shot = clear_shot(monster);
  if (!shot.clear || shot.crossed_water) { qa_q1mpm_monster_play(monster, "armagon_run1"); return; }
  if (game->time < monster->state.attack_finished) return;
  if (shot.distance < 200 && monster->state.enemy.has && game->is_player(game, monster->state.enemy)) {
    repulse(monster);
    return;
  }
  qa_q1mpm_number(monster, "state", 0);
  if (shot.distance > 450) { qa_q1mpm_monster_play(monster, "armagon_run1"); return; }
  qa_q1mpm_monster_play(monster, game->host->random(game->host->ctx) < 0.5 ? "armagon_satk1" : "armagon_slaser1");
  if (qa_q1mpm_actor_number(entity, "cnt") == 1) qa_q1mpm_monster_play(monster, "armagon_run1");
}
static bool check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  monster->lefty = false;
  clear_shot_result shot = clear_shot(monster);
  if ((!shot.clear && qa_q1mpm_actor_number(entity, "charmed") == 0) || shot.crossed_water ||
      game->time < monster->state.attack_finished)
    return false;
  double delta = entity->ideal_yaw - (game->body(game, entity).angles.y + qa_q1mpm_actor_number(entity, "fixangle"));
  if ((fabs(delta) > 10 && shot.distance > 200) || !monster->state.enemy.has ||
      !game->is_player(game, monster->state.enemy))
    return false;
  if (shot.distance < 400) {
    qa_q1mpm_monster_play(monster, "armagon_stop1");
    return true;
  }
  monster->lefty = true;
  return false;
}
static void body_explode(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *body) {
  (void)ctx;
  game->schedule(game, body, 0.1, game->named.action(game->named.ctx, body, "hipnotic:armagon_body_explode1"));
  double count = qa_q1mpm_actor_number(body, "cnt");
  if (count == 0) body->count = 0;
  if (count < 25) {
    if (count > body->count) {
      static const char *const models[] = {"gib1", "gib2", "gib3"};
      for (size_t i = 0; i < 3; i++)
        qa_q1mpm_ext_throw_gib(game, game->body(game, body).origin, models[i], -100);
      body->count = count + 1;
    }
    char buf[32];
    qa_q1mpm_format_number(buf, sizeof(buf), count + 1);
    qa_q1mpm_fields_set(body, "cnt", buf);
  } else {
    qa_q1mpm_fields_set(body, "cnt", "0");
    game->schedule(game, body, 0.1, game->named.action(game->named.ctx, body, "hipnotic:armagon_body_explode2"));
  }
}
static void body_explosion(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *body) {
  (void)ctx;
  game->sound(game, body, "misc/longexpl.wav", QA_Q1MPM_CH("auto"), 0.5, 1);
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (int i = 0; i < 3; i++)
    for (size_t j = 0; j < 3; j++)
      qa_q1mpm_ext_throw_gib(game, game->body(game, body).origin, models[j], -200);
  body->movement = "none";
  snprintf(body->model, sizeof(body->model), "%s", "progs/s_explod.spr");
  body->solid = "none";
  body->frame = 0;
  game->schedule(game, body, 0.1, game->named.action(game->named.ctx, body, "base:explosion_frame"));
}
static void final_death(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  think(monster);
  qa_q1mpm_ext_multi_explosion(game, entity,
                               qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 80)),
                               20, 10, 3, 0.1, 0.5);
  game->cancel(game, entity);
  entity->movement = "none";
  qa_q1mpm_actor_set_damageable(game, entity, false);
  entity->solid = "none";
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, 32}};
  game->set_bounds(game, entity, bounds);
  entity->movement_flags = 0;
  qa_q1mpm_fields_set(entity, "gorging", "1");
  entity->wait = game->time + 5;
  qa_q1mpm_actor *body = body_part(monster);
  game->cancel(game, body);
  qa_q1mpm_actor_set_damageable(game, body, false);
  body->solid = "none";
  qa_q1mpm_fields_set(body, "gorging", "1");
  game->schedule(game, body, 0.1, game->named.action(game->named.ctx, body, "hipnotic:armagon_body_explode1"));
}
static void servo(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  sound(monster, "armagon/servo.wav", QA_Q1MPM_CH_NUM(7), 0.5, 0.5);
}
static void foot(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  sound(monster, "armagon/footfall.wav", QA_Q1MPM_CH_NUM(6), 1, 0.5);
}
static void missile_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, monster->game->host->random(monster->game->host->ctx) < 0.5
                                     ? "armagon_watk1" : "armagon_wlaseratk1");
}
static void stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_STAND, 0);
  think(monster);
  qa_q1mpm_monster_delay(monster, 0.2);
}
static void stand2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  think(monster);
  qa_q1mpm_monster_delay(monster, 0.2);
}
static void walk3(void *ctx, qa_q1mpm_mission_monster *monster) {
  servo(ctx, monster);
  walk(ctx, monster);
  walk_think_impl(monster);
}
static void walk5(void *ctx, qa_q1mpm_mission_monster *monster) {
  foot(ctx, monster);
  walk(ctx, monster);
  walk_think_impl(monster);
}
static void run3(void *ctx, qa_q1mpm_mission_monster *monster) { servo(ctx, monster); run_impl(monster); }
static void run5(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); run_impl(monster); }
static void run12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  run_impl(monster);
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_actor_number(entity, "cnt") == 1 && game->time > monster->state.attack_finished) {
    double delta = entity->ideal_yaw - game->body(game, entity).angles.y;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    qa_q1mpm_monster_set_next_frame(monster, delta > 0 ? "armagon_overleft1" : "armagon_overright1");
  } else if (monster->lefty) {
    monster->lefty = false;
    qa_q1mpm_monster_set_next_frame(monster, "armagon_missile_attack");
  }
}
static void watk1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; walking_attack_impl(monster); }
static void watk2(void *ctx, qa_q1mpm_mission_monster *monster) { servo(ctx, monster); walking_attack_impl(monster); }
static void watk4(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); walking_attack_impl(monster); }
static void watk6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  walking_attack_impl(monster);
  launch(monster, 40, 0, false);
}
static void watk11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  walking_attack_impl(monster);
  launch(monster, -40, 0, false);
}
static void watk13(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  walking_attack_impl(monster);
  qa_q1mpm_monster_attack_finished(monster, 1);
}
static void wlaseratk6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  walking_attack_impl(monster);
  launch(monster, 40, 0, true);
}
static void wlaseratk11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  walking_attack_impl(monster);
  launch(monster, -40, 0, true);
}
static void attack_finished_1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_attack_finished(monster, 1);
}
static void attack_finished_03(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_attack_finished(monster, 0.3);
}
static void stop2(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); think(monster); }
static void satk9(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  think(monster);
  launch(monster, 40, 0, false);
  launch(monster, -40, 0, false);
}
static void laser40(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; launch(monster, 40, 0, true); }
static void laser_neg40(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; launch(monster, -40, 0, true); }
static void die4_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  think(monster);
  qa_q1mpm_ext_multi_explosion(monster->game, monster->entity,
                               qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 48)),
                               48, 10, 6, 0.3, 0.3);
  sound(monster, "armagon/death.wav", QA_Q1MPM_CH("auto"), 1, 0);
  qa_q1mpm_monster_delay(monster, 0.2);
}
static void die8(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  think(monster);
  qa_q1mpm_monster_delay(monster, 2);
}
static bool over_left_flag = true, over_right_flag = false;
static void over1(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->entity->count = 0;
  over_think(monster, *(const bool *)ctx);
}
static void over3(void *ctx, qa_q1mpm_mission_monster *monster) {
  servo(NULL, monster);
  over_think(monster, *(const bool *)ctx);
}
static void overleft5(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); over_think(monster, true); }
static void overleft11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->count = 1;
  over_think(monster, true);
}
static void overleft12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->count = 2;
  over_think(monster, true);
}
static void overright5(void *ctx, qa_q1mpm_mission_monster *monster) {
  foot(ctx, monster);
  monster->entity->count = 1;
  over_think(monster, false);
}
static void overright6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->count = 2;
  over_think(monster, false);
}
static void overright10(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); over_think(monster, false); }
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  int skill = game->options.skill;
  qa_q1mpm_actor *body = game->create(game, "armagon_body");
  monster->lefty = false;
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vsub(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 64));
  patch.has_bounds = true;
  patch.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
  game->set_body(game, body, &patch);
  if (qa_q1mpm_str_eq(game->options.edition, "rerelease")) {
    qa_vec3 origin = qa_q1mpm_monster_origin(monster);
    char buf[128];
    char fx[32], fy[32], fz[32];
    qa_q1mpm_format_number(fx, sizeof(fx), origin.x);
    qa_q1mpm_format_number(fy, sizeof(fy), origin.y);
    qa_q1mpm_format_number(fz, sizeof(fz), origin.z);
    snprintf(buf, sizeof(buf), "%s %s %s", fx, fy, fz);
    qa_q1mpm_fields_set(body, "oldorigin", buf);
  } else {
    body->movement = "step";
  }
  body->solid = "none";
  snprintf(body->model, sizeof(body->model), "%s", "progs/armabody.mdl");
  qa_q1mpm_references_set(entity, "trigger_field", QA_Q1MPM_SOME(body->actor.id));
  qa_q1mpm_references_set(body, "trigger_field", QA_Q1MPM_SOME(entity->actor.id));
  qa_q1mpm_number(monster, "fixangle", 0);
  qa_q1mpm_number(monster, "yaw_speed", skill == 0 ? 5 : skill == 1 ? 9 : 12);
  qa_q1mpm_number(monster, "worldtype", skill == 0 ? 0.9 : skill == 1 ? 0.85 : 0.75);
  qa_q1mpm_number(monster, "state", 0);
  qa_q1mpm_number(monster, "super_time", 0);
  qa_q1mpm_number(monster, "endtime", 0);
  game->link(game, body);
  qa_q1mpm_monster_spawn_default(monster);
}
static void found(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  (void)ctx;
  qa_q1mpm_monster_found_default(monster, target);
  sound(monster, "armagon/sight.wav", QA_Q1MPM_CH("voice"), 1, 0.1);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, "armagon_stop1");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker;
  if (monster->game->health(monster->game, monster->entity->actor.id) <= 0 || damage < 25 ||
      monster->state.pain_finished > monster->game->time)
    return;
  monster->state.pain_finished = monster->game->time + 2;
  qa_q1mpm_game_sound(monster->game, monster->entity, "armagon/pain.wav");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_monster_play(monster, "armagon_die1");
}
static const char *const classnames[] = {"monster_armagon"};
static const qa_q1mpm_named_action actions[] = {
    {"armagon_think", {think_action, NULL}},
    {"armagon_walkthink", {walk_think, NULL}},
    {"armagon_overleft_think", {over_left, NULL}},
    {"armagon_overright_think", {over_right, NULL}},
    {"armagon_stand_attack", {stand_attack, NULL}},
    {"armagon_missile_attack", {missile_attack, NULL}},
    {"movetogoal(14)", {walk, NULL}},
    {"hiparma:armagon_stand1", {stand1, NULL}},
    {"hiparma:armagon_stand2", {stand2, NULL}},
    {"hiparma:armagon_walk3", {walk3, NULL}},
    {"hiparma:armagon_walk5", {walk5, NULL}},
    {"hiparma:armagon_run1", {run_action, NULL}},
    {"hiparma:armagon_run3", {run3, NULL}},
    {"hiparma:armagon_run5", {run5, NULL}},
    {"hiparma:armagon_run12", {run12, NULL}},
    {"hiparma:armagon_watk1", {watk1, NULL}},
    {"hiparma:armagon_watk2", {watk2, NULL}},
    {"hiparma:armagon_watk4", {watk4, NULL}},
    {"hiparma:armagon_watk6", {watk6, NULL}},
    {"hiparma:armagon_watk11", {watk11, NULL}},
    {"hiparma:armagon_watk13", {watk13, NULL}},
    {"hiparma:armagon_wlaseratk6", {wlaseratk6, NULL}},
    {"hiparma:armagon_wlaseratk11", {wlaseratk11, NULL}},
    {"SUB_AttackFinished(1.0)", {attack_finished_1, NULL}},
    {"SUB_AttackFinished(0.3)", {attack_finished_03, NULL}},
    {"hiparma:armagon_stop2", {stop2, NULL}},
    {"hiparma:armagon_satk6", {stop2, NULL}},
    {"hiparma:armagon_satk9", {satk9, NULL}},
    {"armagon_launch_laser(40)", {laser40, NULL}},
    {"armagon_launch_laser(-40)", {laser_neg40, NULL}},
    {"hiparma:armagon_die4", {die4_action, NULL}},
    {"hiparma:armagon_die8", {die8, NULL}},
    {"hiparma:armagon_die14", {final_death, NULL}},
    {"hiparma:armagon_overleft1", {over1, &over_left_flag}},
    {"hiparma:armagon_overleft3", {over3, &over_left_flag}},
    {"hiparma:armagon_overright1", {over1, &over_right_flag}},
    {"hiparma:armagon_overright3", {over3, &over_right_flag}},
    {"hiparma:armagon_overleft5", {overleft5, NULL}},
    {"hiparma:armagon_overleft11", {overleft11, NULL}},
    {"hiparma:armagon_overleft12", {overleft12, NULL}},
    {"hiparma:armagon_overright5", {overright5, NULL}},
    {"hiparma:armagon_overright6", {overright6, NULL}},
    {"hiparma:armagon_overright10", {overright10, NULL}},
};
void qa_q1mpm_armagon_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  int skill = runtime->game->options.skill;
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1mpm_species_spec){
      "armagon", NULL, classnames, 1, "armalegs", NULL,
      skill == 0 ? 2000 : skill == 1 ? 2500 : 3500, -INFINITY, NULL, 0,
      {{-48, -48, -24}, {48, 48, 84}}, "armagon_stand1", "armagon_walk1", "armagon_run1",
      "", "armagon_missile_attack", true, "walk"};
  out->frames = (qa_q1mpm_frame_table){qa_q1mpm_tables_hiparma_entries, QA_Q1MPM_TABLES_HIPARMA_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = calloc(2, sizeof(*callbacks));
  if (callbacks != NULL) {
    callbacks[0].name = "armagon_body_explode1";
    callbacks[0].handlers = (qa_q1mpm_handlers){NULL, body_explode, NULL, NULL, NULL, NULL};
    callbacks[1].name = "armagon_body_explode2";
    callbacks[1].handlers = (qa_q1mpm_handlers){NULL, body_explosion, NULL, NULL, NULL, NULL};
    out->callbacks = callbacks;
    out->callback_count = 2;
  }
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_action, NULL};
  out->check_attack = check_attack;
  out->found = found;
}
