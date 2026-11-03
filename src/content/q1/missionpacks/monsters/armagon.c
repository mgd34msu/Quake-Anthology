/* Port of src/content/q1/missionpacks/monsters/armagon.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/armagon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hiparma.h"
#include "monsters_internal.h"

static qa_q1_actor *body_part(qa_q1mpm_mission_monster *monster) {
  qa_actor_id field;
  qa_q1_actor *body = qa_q1_get_entity(monster->base.game, q1mpm_reference(monster->base.entity, "trigger_field", &field));
  if (body == NULL) qa_range_error("Armagon lost its source body entity");
  return body;
}
/* TS sound(monster, path, channel, volume = 1, attenuation = 0.5). */
static void sound(qa_q1mpm_mission_monster *monster, const char *path, qa_q1_sound_channel channel,
                  double volume, double attenuation) {
  q1mpm_emit_sound(monster->base.game, monster->base.entity->actor.id, path, channel, volume, attenuation);
}
static qa_vec3 enemy_origin(qa_q1mpm_mission_monster *monster) {
  qa_vec3 target;
  return qa_q1_base_monster_target(&monster->base, &target) ? target : QA_Q1_ZERO;
}
static qa_vec3 enemy_eye(qa_q1mpm_mission_monster *monster) {
  if (!monster->base.state->has_enemy) return QA_Q1_ZERO;
  qa_vec3 value;
  return qa_q1mpm_eye(monster->base.game, monster->base.state->enemy, &value) ? value : QA_Q1_ZERO;
}
static void turn(qa_q1mpm_mission_monster *monster, double difference, double target) {
  double current = qa_q1_actor_number(monster->base.entity, "fixangle");
  qa_q1mpm_number(monster, "fixangle",
                  fabs(difference) < 10 ? target : difference > 5 ? current + 9
                                              : difference < -5   ? current - 9
                                                                  : target);
}
static qa_q1_actor *sync_body(qa_q1mpm_mission_monster *monster, bool walking) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *body = body_part(monster);
  qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  qa_q1_set_origin(game, body, qa_q1_base_monster_origin(&monster->base));
  if (walking && q1mpm_str_eq(game->options.edition, "rerelease")) body->movement = "step";
  body->frame = entity->frame;
  qa_q1_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = angles;
  patch.angles.y = angles.y + qa_q1_actor_number(entity, "fixangle");
  qa_q1_set_body(game, body, &patch);
  return body;
}
static void idle_sound(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_health(game, entity->actor.id) < 0 || game->time <= qa_q1_actor_number(entity, "super_time"))
    return;
  qa_q1mpm_number(monster, "super_time", game->time + 3);
  if (qa_q1_host_random(game) < 0.5) {
    double r = qa_q1_host_random(game);
    char path[32];
    snprintf(path, sizeof(path), "armagon/idle%d.wav", r < 0.25 ? 1 : r < 0.5 ? 2 : r < 0.75 ? 3 : 4);
    sound(monster, path, "voice", 1, 0.5);
  }
}
static void think(qa_q1mpm_mission_monster *monster) {
  qa_vec3 a2;
  qa_vec3 b2;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *body = sync_body(monster, false);
  qa_vec3 a = enemy_origin(monster);
  qa_vec3 b = qa_q1_base_monster_origin(&monster->base);
  entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(a, b));
  double delta = entity->ideal_yaw - qa_q1_get_body(game, entity).angles.y;
  qa_q1mpm_number(monster, "cnt", 0);
  if (delta > 180) delta -= 360;
  if (delta < -180) delta += 360;
  if (fabs(delta) > 90) { delta = 0; qa_q1mpm_number(monster, "cnt", 1); }
  turn(monster, delta - qa_q1_actor_number(entity, "fixangle"), delta);
  if (qa_q1_health(game, entity->actor.id) < 0) return;
  idle_sound(monster);
  if (q1mpm_str_eq(game->options.edition, "rerelease") &&
      qa_q1_length((a2 = qa_q1_actor_vector(body, "oldorigin"), b2 = qa_q1_get_body(game, body).origin, qa_q1_vsub(a2, b2))) > 50)
    body->movement = "step";
}
static void think_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; think(monster); }
static void walk_think_impl(qa_q1mpm_mission_monster *monster) {
  sync_body(monster, true);
  qa_q1_base_monster_change_yaw(&monster->base);
  qa_q1mpm_number(monster, "cnt", 0);
  turn(monster, -qa_q1_actor_number(monster->base.entity, "fixangle"), 0);
  if (qa_q1_health(monster->base.game, monster->base.entity->actor.id) < 0) return;
  idle_sound(monster);
  qa_actor_id client;
  if (q1mpm_check_client(monster->base.game, monster->base.entity->actor, &client) &&
      qa_q1_base_monster_visible(&monster->base, &client))
    qa_q1_base_monster_found(&monster->base, client);
}
static void walk_think(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; walk_think_impl(monster); }
static void launch(qa_q1mpm_mission_monster *monster, double offset, int turn_direction, bool laser) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  angles.y = angles.y + qa_q1_actor_number(entity, "fixangle") +
             (turn_direction == 1 ? 165 : turn_direction == 2 ? -165 : 0);
  qa_q1_basis basis = qa_q1_make_vectors(game, angles);
  qa_vec3 origin = qa_q1_vadd(
      qa_q1_vadd(qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 66}),
                    qa_q1_vscale(basis.right, offset)),
      qa_q1_vscale(basis.forward, 84));
  qa_vec3 target = enemy_eye(monster);
  if (qa_q1_option_skill(game) != 0) {
    qa_q1_body enemy_body;
    qa_vec3 velocity = monster->base.state->has_enemy &&
                               q1mpm_read_body(game, monster->base.state->enemy, &enemy_body)
                           ? enemy_body.velocity
                           : QA_Q1_ZERO;
    target = qa_q1_vadd(target, qa_q1_vscale(velocity, qa_q1_length(qa_q1_vsub(target, origin)) / 1000));
  }
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target, origin));
  if (qa_q1_dot(direction, basis.forward) < qa_q1_actor_number(entity, "worldtype"))
    direction = basis.forward;
  entity->effects = qa_js_bit_or(entity->effects, 2);
  if (laser) {
    (void)qa_q1mp_launch_hipnotic_laser(game, entity->actor.id, origin, direction, false, NULL);
  } else {
    qa_q1_sound(game, entity->actor.id, "weapons/sgun1.wav", "weapon", 1, 1);
    qa_vec3 punch = qa_q1_actor_vector(entity, "punchangle");
    char y[40], z[40], text[96];
    qa_net_js_number(punch.y, y, sizeof y);
    qa_net_js_number(punch.z, z, sizeof z);
    snprintf(text, sizeof text, "-2 %s %s", y, z);
    (void)qa_q1_actor_field_set(entity, "punchangle", text);
    qa_q1_actor *shot = qa_q1_create_missile(game, &entity->actor.id, "missile", "missile", origin, qa_q1_vscale(direction, 1000));
    shot->projectile = "rocket";
    shot->touch = qa_q1_callback_registry_touch(game->named, shot, "projectile_touch");
  }
}
static void over_think(qa_q1mpm_mission_monster *monster, bool left) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_base_monster_change_yaw(&monster->base);
  (void)game->host.walk_move(game->host.ctx, entity->actor, qa_q1_get_body(game, entity).angles.y, 14);
  (void)sync_body(monster, false);
  double delta = 0;
  if (entity->count == 0) {
    qa_vec3 a = enemy_origin(monster);
    qa_vec3 b = qa_q1_base_monster_origin(&monster->base);
    entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(a, b));
    delta = entity->ideal_yaw - qa_q1_get_body(game, entity).angles.y + (left ? -165 : 165);
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
  } else if (entity->count == 1) {
    launch(monster, left ? 40 : -40, left ? 1 : 2, false);
    return;
  }
  turn(monster, delta - qa_q1_actor_number(entity, "fixangle"), delta);
}
static void over_left(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; over_think(monster, true); }
static void over_right(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; over_think(monster, false); }
static void walk(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *found[1];
  const qa_actor_id *goal = qa_q1_find(game, monster->base.state->path, found, 1) > 0 ? &found[0]->actor.id
                            : game->world == NULL ? NULL
                                                  : &game->world->actor.id;
  if (goal != NULL) game->host.move_to_goal(game->host.ctx, entity->actor, *goal, 14, NULL);
}
static void run_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_actor *world = monster->base.game->world;
  if (world == NULL) qa_range_error("Armagon requires a world entity");
  qa_q1_base_monster_change_yaw(&monster->base);
  (void)qa_q1_actor_field_set(world, "RUN_STRAIGHT", "1");
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_RUN, 14);
  think(monster);
}
static void run_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; run_impl(monster); }
static void walking_attack_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_base_monster_change_yaw(&monster->base);
  (void)monster->base.game->host.walk_move(monster->base.game->host.ctx, monster->base.entity->actor,
                                           qa_q1_get_body(monster->base.game, monster->base.entity).angles.y, 14);
  think(monster);
}
static void repulse(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  think(monster);
  if (qa_q1_actor_number(entity, "state") == 0) {
    qa_q1_base_monster_attack_finished(&monster->base, 0.5);
    qa_q1_sound(game, entity->actor.id, "armagon/repel.wav", "body", 1, 1);
    qa_q1mpm_number(monster, "state", 1);
    return;
  }
  if (qa_q1_actor_number(entity, "state") != 1) return;
  size_t count = 0;
  qa_actor_id *actors = qa_q1mpm_radius_actors(game, qa_q1_base_monster_origin(&monster->base), 300, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id actor = actors[i];
    if (!qa_q1_is_player(game, &actor)) continue;
    qa_q1_actor *other = qa_q1_get_entity(game, &actor);
    if (qa_js_bit_and(other == NULL ? 0 : other->movement_flags, 128) != 0) continue;
    if (!qa_q1_base_monster_visible(&monster->base, &actor) || qa_q1_health(game, actor) <= 0) continue;
    qa_owned_actor owner;
    const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner);
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
    if (!has_owner || !body.has_value) continue;
    const qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(
        body.value.origin, qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 24})));
    qa_body_state next = body.value; /* TS { ...body, velocity } */
    next.velocity = qa_q1_vadd(body.value.velocity, qa_q1_vscale(direction, 1500));
    qa_shared_body_table_write(game->host.bodies, owner, next);
  }
  free(actors);
  qa_q1_radius_damage(game, entity->actor.id, &entity->actor.id, 60, &entity->actor.id, NULL, NULL);
  qa_q1mpm_number(monster, "state", 0);
  qa_q1_base_monster_attack_finished(&monster->base, 0.1);
}
typedef struct clear_shot_result { bool clear; bool crossed_water; double distance; } clear_shot_result;
static clear_shot_result clear_shot(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 start;
  if (!qa_q1_base_monster_eye(&monster->base, monster->base.entity->actor.id, &start)) start = qa_q1_base_monster_origin(&monster->base);
  qa_vec3 end = enemy_eye(monster);
  const qa_actor_id *enemy = monster->base.state->has_enemy ? &monster->base.state->enemy
                             : game->world == NULL ? NULL
                                                   : &game->world->actor.id;
  const qa_q1_trace trace = q1mpm_trace(game, start, end, QA_Q1_POINT, &entity->actor.id, true);
  clear_shot_result result = {enemy != NULL && trace.has_actor && qa_same_actor(trace.actor, *enemy),
                              trace.in_open && trace.in_water, qa_q1_length(qa_q1_vsub(end, start))};
  return result;
}
static void stand_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  clear_shot_result shot = clear_shot(monster);
  if (!shot.clear || shot.crossed_water) { qa_q1_base_monster_play(&monster->base, "armagon_run1"); return; }
  if (game->time < monster->base.state->attack_finished) return;
  if (shot.distance < 200 && monster->base.state->has_enemy && qa_q1_is_player(game, qa_q1_base_monster_enemy(&monster->base))) {
    repulse(monster);
    return;
  }
  qa_q1mpm_number(monster, "state", 0);
  if (shot.distance > 450) { qa_q1_base_monster_play(&monster->base, "armagon_run1"); return; }
  qa_q1_base_monster_play(&monster->base, qa_q1_host_random(game) < 0.5 ? "armagon_satk1" : "armagon_slaser1");
  if (qa_q1_actor_number(entity, "cnt") == 1) qa_q1_base_monster_play(&monster->base, "armagon_run1");
}
static bool check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  double left;
  double right;
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  monster->base.lefty = false;
  clear_shot_result shot = clear_shot(monster);
  if ((!shot.clear && qa_q1_actor_number(entity, "charmed") == 0) || shot.crossed_water ||
      game->time < monster->base.state->attack_finished)
    return false;
  double delta = entity->ideal_yaw - ((left = qa_q1_get_body(game, entity).angles.y, right = qa_q1_actor_number(entity, "fixangle"), left + right));
  if ((fabs(delta) > 10 && shot.distance > 200) || !monster->base.state->has_enemy ||
      !qa_q1_is_player(game, qa_q1_base_monster_enemy(&monster->base)))
    return false;
  if (shot.distance < 400) {
    qa_q1_base_monster_play(&monster->base, "armagon_stop1");
    return true;
  }
  monster->base.lefty = true;
  return false;
}
static void body_explode(void *ctx, qa_q1_services *game, qa_q1_actor *body) {
  (void)ctx;
  qa_q1_schedule(game, body, 0.1, qa_q1_callback_registry_action(game->named, body, "hipnotic:armagon_body_explode1"));
  double count = qa_q1_actor_number(body, "cnt");
  if (count == 0) body->count = 0;
  if (count < 25) {
    if (count > body->count) {
      static const char *const models[] = {"gib1", "gib2", "gib3"};
      for (size_t i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, qa_q1_get_body(game, body).origin, models[i], -100);
      body->count = count + 1;
    }
    q1mpm_set_number_text(body, "cnt", count + 1);
  } else {
    (void)qa_q1_actor_field_set(body, "cnt", "0");
    qa_q1_schedule(game, body, 0.1, qa_q1_callback_registry_action(game->named, body, "hipnotic:armagon_body_explode2"));
  }
}
static void body_explosion(void *ctx, qa_q1_services *game, qa_q1_actor *body) {
  (void)ctx;
  qa_q1_sound(game, body->actor.id, "misc/longexpl.wav", "auto", 0.5, 1);
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (int i = 0; i < 3; i++)
    for (size_t j = 0; j < 3; j++)
      (void)qa_q1_throw_gib(game, qa_q1_get_body(game, body).origin, models[j], -200);
  body->movement = "none";
  q1mpm_set_text(&body->model, "progs/s_explod.spr");
  body->solid = "none";
  body->frame = 0;
  qa_q1_schedule(game, body, 0.1, qa_q1_callback_registry_action(game->named, body, "base:explosion_frame"));
}
static void final_death(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  think(monster);
  (void)qa_q1mp_multi_explosion(game, entity, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 80}),
                                20, 10, 3, 0.1, 0.5);
  qa_q1_cancel(game, entity);
  entity->movement = "none";
  qa_q1_actor_set_damageable(entity, false);
  entity->solid = "none";
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, 32}};
  qa_q1_set_bounds(game, entity, bounds);
  entity->movement_flags = 0;
  (void)qa_q1_actor_field_set(entity, "gorging", "1");
  entity->wait = game->time + 5;
  qa_q1_actor *body = body_part(monster);
  qa_q1_cancel(game, body);
  qa_q1_actor_set_damageable(body, false);
  body->solid = "none";
  (void)qa_q1_actor_field_set(body, "gorging", "1");
  qa_q1_schedule(game, body, 0.1, qa_q1_callback_registry_action(game->named, body, "hipnotic:armagon_body_explode1"));
}
static void servo(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  sound(monster, "armagon/servo.wav", "7", 0.5, 0.5);
}
static void foot(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  sound(monster, "armagon/footfall.wav", "6", 1, 0.5);
}
static void missile_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, qa_q1_host_random(monster->base.game) < 0.5
                                     ? "armagon_watk1" : "armagon_wlaseratk1");
}
static void stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_STAND, 0);
  think(monster);
  qa_q1_base_monster_delay(&monster->base, 0.2);
}
static void stand2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  think(monster);
  qa_q1_base_monster_delay(&monster->base, 0.2);
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
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_actor_number(entity, "cnt") == 1 && game->time > monster->base.state->attack_finished) {
    double delta = entity->ideal_yaw - qa_q1_get_body(game, entity).angles.y;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    q1mpm_set_next_frame(&monster->base, delta > 0 ? "armagon_overleft1" : "armagon_overright1");
  } else if (monster->base.lefty) {
    monster->base.lefty = false;
    q1mpm_set_next_frame(&monster->base, "armagon_missile_attack");
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
  qa_q1_base_monster_attack_finished(&monster->base, 1);
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
  qa_q1_base_monster_attack_finished(&monster->base, 1);
}
static void attack_finished_03(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_attack_finished(&monster->base, 0.3);
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
  (void)qa_q1mp_multi_explosion(monster->base.game, monster->base.entity,
                                qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 48}), 48, 10, 6,
                                0.3, 0.3);
  sound(monster, "armagon/death.wav", "auto", 1, 0);
  qa_q1_base_monster_delay(&monster->base, 0.2);
}
static void die8(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  think(monster);
  qa_q1_base_monster_delay(&monster->base, 2);
}
static const bool OVER_LEFT = true, OVER_RIGHT = false;
static void over1(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->base.entity->count = 0;
  over_think(monster, *(const bool *)ctx);
}
static void over3(void *ctx, qa_q1mpm_mission_monster *monster) {
  servo(NULL, monster);
  over_think(monster, *(const bool *)ctx);
}
static void overleft5(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); over_think(monster, true); }
static void overleft11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->count = 1;
  over_think(monster, true);
}
static void overleft12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->count = 2;
  over_think(monster, true);
}
static void overright5(void *ctx, qa_q1mpm_mission_monster *monster) {
  foot(ctx, monster);
  monster->base.entity->count = 1;
  over_think(monster, false);
}
static void overright6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->count = 2;
  over_think(monster, false);
}
static void overright10(void *ctx, qa_q1mpm_mission_monster *monster) { foot(ctx, monster); over_think(monster, false); }
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const double skill = qa_q1_option_skill(game);
  qa_q1_actor *body = qa_q1_create(game, "armagon_body", NULL, NULL);
  monster->base.lefty = false;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 64});
  patch.has_bounds = true;
  patch.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
  qa_q1_set_body(game, body, &patch);
  if (q1mpm_str_eq(game->options.edition, "rerelease")) {
    qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
    char x[40], y[40], z[40], text[128];
    qa_net_js_number(origin.x, x, sizeof x);
    qa_net_js_number(origin.y, y, sizeof y);
    qa_net_js_number(origin.z, z, sizeof z);
    snprintf(text, sizeof text, "%s %s %s", x, y, z);
    (void)qa_q1_actor_field_set(body, "oldorigin", text);
  } else {
    body->movement = "step";
  }
  body->solid = "none";
  q1mpm_set_text(&body->model, "progs/armabody.mdl");
  (void)qa_q1_actor_reference_set(entity, "trigger_field", &body->actor.id);
  (void)qa_q1_actor_reference_set(body, "trigger_field", &entity->actor.id);
  qa_q1mpm_number(monster, "fixangle", 0);
  qa_q1mpm_number(monster, "yaw_speed", skill == 0 ? 5 : skill == 1 ? 9 : 12);
  qa_q1mpm_number(monster, "worldtype", skill == 0 ? 0.9 : skill == 1 ? 0.85 : 0.75);
  qa_q1mpm_number(monster, "state", 0);
  qa_q1mpm_number(monster, "super_time", 0);
  qa_q1mpm_number(monster, "endtime", 0);
  qa_q1_link(game, body);
  qa_q1mpm_monster_spawn_default(monster);
}
static void found(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  (void)ctx;
  qa_q1mpm_monster_found_default(monster, target);
  sound(monster, "armagon/sight.wav", "voice", 1, 0.1);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, "armagon_stop1");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker;
  if (qa_q1_health(monster->base.game, monster->base.entity->actor.id) <= 0 || damage < 25 ||
      monster->base.state->pain_finished > monster->base.game->time)
    return;
  monster->base.state->pain_finished = monster->base.game->time + 2;
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "armagon/pain.wav");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_base_monster_play(&monster->base, "armagon_die1");
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
    {"SUB_AttackFinished(1)", {attack_finished_1, NULL}},
    {"SUB_AttackFinished(0.3)", {attack_finished_03, NULL}},
    {"hiparma:armagon_stop2", {stop2, NULL}},
    {"hiparma:armagon_satk6", {stop2, NULL}},
    {"hiparma:armagon_satk9", {satk9, NULL}},
    {"armagon_launch_laser(40)", {laser40, NULL}},
    {"armagon_launch_laser(-40)", {laser_neg40, NULL}},
    {"hiparma:armagon_die4", {die4_action, NULL}},
    {"hiparma:armagon_die8", {die8, NULL}},
    {"hiparma:armagon_die14", {final_death, NULL}},
    {"hiparma:armagon_overleft1", {over1, (void *)&OVER_LEFT}},
    {"hiparma:armagon_overleft3", {over3, (void *)&OVER_LEFT}},
    {"hiparma:armagon_overright1", {over1, (void *)&OVER_RIGHT}},
    {"hiparma:armagon_overright3", {over3, (void *)&OVER_RIGHT}},
    {"hiparma:armagon_overleft5", {overleft5, NULL}},
    {"hiparma:armagon_overleft11", {overleft11, NULL}},
    {"hiparma:armagon_overleft12", {overleft12, NULL}},
    {"hiparma:armagon_overright5", {overright5, NULL}},
    {"hiparma:armagon_overright6", {overright6, NULL}},
    {"hiparma:armagon_overright10", {overright10, NULL}},
};
void qa_q1mpm_armagon_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  const double skill = qa_q1_option_skill(runtime->game);
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1_base_monster_species){
      "armagon", NULL, classnames, 1, "armalegs", NULL,
      skill == 0 ? 2000 : skill == 1 ? 2500 : 3500, -INFINITY, NULL, 0,
      {{-48, -48, -24}, {48, 48, 84}}, "armagon_stand1", "armagon_walk1", "armagon_run1",
      "", "armagon_missile_attack", true, "walk"};
  out->frames = (qa_q1_monster_frame_table){qa_q1mpm_tables_hiparma_entries, QA_Q1MPM_TABLES_HIPARMA_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = qa_q1mpm_runtime_calloc(runtime, 2, sizeof *callbacks);
  callbacks[0].name = "armagon_body_explode1";
  callbacks[0].handlers = (qa_q1_callback_handlers){.action = body_explode};
  callbacks[1].name = "armagon_body_explode2";
  callbacks[1].handlers = (qa_q1_callback_handlers){.action = body_explosion};
  out->callbacks = callbacks;
  out->callback_count = 2;
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_action, NULL};
  out->check_attack = check_attack;
  out->found = found;
}
