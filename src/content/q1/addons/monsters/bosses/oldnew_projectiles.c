/* Port of src/content/q1/addons/monsters/bosses/oldnew-projectiles.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/oldnew_projectiles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/sphere_points.h"
#include "qa/foundation/support.h"

void qa_q1am_boss_later(qa_q1am_context *context, qa_q1am_actor *entity, const char *name, double delay) {
  char full[220];
  (void)snprintf(full, sizeof full, "%s%s", QA_Q1AM_OLDNEW_PREFIX, name);
  context->game->schedule(context->game->ctx, entity, delay,
                          context->game->named_action(context->game->ctx, entity, full));
}
bool qa_q1am_boss_enemy(qa_q1am_context *context, qa_q1am_actor *entity, qa_actor_id *out) {
  (void)context;
  if (entity->has_monster && entity->monster.has_enemy) {
    *out = entity->monster.enemy;
    return true;
  }
  return qa_q1am_actor_ref(entity, "enemy", out);
}
qa_actor_id qa_q1am_boss_world(qa_q1am_context *context) {
  if (context->game->world == NULL) qa_range_error("MG3 boss damage requires worldspawn");
  return context->game->world->actor.id;
}
qa_vec3 qa_q1am_boss_target(qa_q1am_context *context, qa_q1am_actor *entity) {
  qa_actor_id enemy;
  if (!qa_q1am_boss_enemy(context, entity, &enemy)) return QA_Q1AM_ZERO;
  qa_q1am_body body;
  return context->game->body_read(context->game->ctx, enemy, &body) ? body.origin : QA_Q1AM_ZERO;
}
static double flat_dot(qa_vec3 a, qa_vec3 b) {
  a.z = 0;
  b.z = 0;
  return qa_dot3(qa_q1am_normalize(a), qa_q1am_normalize(b));
}
static qa_q1am_actor *missile(qa_q1am_context *context, qa_q1am_actor *manager, qa_vec3 origin,
                              qa_vec3 direction, const char *model, qa_vec3 velocity) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *shot = game->launch_spike(game->ctx, game, manager->has_owner, manager->owner, origin, direction);
  (void)snprintf(shot->model, sizeof shot->model, "%s", model);
  shot->touch = game->named_touch(game->ctx, shot, QA_Q1AM_OLDNEW_PREFIX "sphere_mis_touch");
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  game->set_body(game->ctx, shot, &patch);
  return shot;
}
static bool first_player(qa_q1am_game *game, qa_actor_id *out) {
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  bool has = count > 0;
  if (has) *out = players[0];
  free(players);
  return has;
}
qa_q1am_actor *qa_q1am_spawn_sphere_manager(qa_q1am_context *context, qa_q1am_actor *source, double maximum) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *manager = game->create(game->ctx, "sphere_manager");
  manager->classname[0] = '\0';
  manager->has_owner = true;
  manager->owner = source->actor.id;
  game->set_origin(game->ctx, manager, game->body(game->ctx, source).origin);
  qa_actor_id ignored;
  if (!qa_q1am_boss_enemy(context, source, &ignored)) {
    qa_actor_id player;
    bool has_player = first_player(game, &player);
    if (source->has_monster) {
      source->monster.has_enemy = has_player;
      source->monster.enemy = player;
    } else {
      qa_q1am_actor_set_ref(source, "enemy", has_player, player);
    }
  }
  qa_vec3 delta = qa_sub3(qa_q1am_boss_target(context, source), game->body(game->ctx, source).origin);
  delta.z = 0;
  qa_vec3 direction = qa_q1am_normalize(delta);
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, game->body(game->ctx, source).angles);
  context->set_number(context->ctx, manager, "aflag",
                      flat_dot(basis.right, qa_cross3_double(direction, (qa_vec3){0, 0, 1})) > 1 ? 1 : 0);
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1am_velocity_angles(direction);
  game->set_body(game->ctx, manager, &patch);
  manager->count = maximum;
  qa_q1am_boss_later(context, manager, "actual_sphere", 0.1);
  return manager;
}
qa_q1am_actor *qa_q1am_spawn_sphere_chunk_manager(qa_q1am_context *context, qa_q1am_actor *source, double maximum) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *manager = game->create(game->ctx, "sphere_chunk_manager");
  manager->classname[0] = '\0';
  manager->has_owner = true;
  manager->owner = source->actor.id;
  manager->wait = 22.5;
  manager->delay = 0.8;
  manager->count = 0;
  context->set_number(context->ctx, manager, "aflag", 1);
  context->set_number(context->ctx, manager, "cnt", maximum);
  qa_actor_id enemy;
  bool has_enemy = qa_q1am_boss_enemy(context, source, &enemy);
  if (!has_enemy) has_enemy = first_player(game, &enemy);
  qa_q1am_actor_set_ref(manager, "enemy", has_enemy, enemy);
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = game->body(game->ctx, source).origin;
  patch.has_angles = true;
  patch.angles = game->body(game->ctx, source).angles;
  game->set_body(game->ctx, manager, &patch);
  qa_q1am_boss_later(context, manager, "sphere_chunk", 0.1);
  return manager;
}
void qa_q1am_auto_gun(qa_q1am_context *context, qa_q1am_actor *source, qa_vec3 origin, double offset) {
  qa_q1am_game *game = context->game;
  qa_actor_id player;
  bool has_player = first_player(game, &player);
  qa_q1am_body body;
  qa_vec3 target = has_player && game->body_read(game->ctx, player, &body) ? body.origin : QA_Q1AM_ZERO;
  qa_vec3 direction = qa_q1am_normalize(qa_sub3(target, origin));
  if (offset != 0)
    direction = qa_add3(qa_scale3(qa_cross3_double(direction, (qa_vec3){0, 0, 1}), offset),
                        qa_scale3(direction, 1 - fabs(offset)));
  qa_q1am_actor *shot = game->launch_spike(game->ctx, game, true, source->actor.id, origin, direction);
  shot->touch = game->named_touch(game->ctx, shot, QA_Q1AM_OLDNEW_PREFIX "sphere_mis_touch");
  (void)snprintf(shot->model, sizeof shot->model, "%s", "progs/rogue/sphere.mdl");
  shot->effects = 64;
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, offset == 0 ? 600 : 800);
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  game->set_body(game->ctx, shot, &patch);
}
qa_q1am_actor *qa_q1am_spawn_boss_teledeath(qa_q1am_context *context, qa_vec3 origin, qa_q1am_actor *owner) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *death = game->create(game->ctx, "teledeath");
  qa_bounds bounds = game->body(game->ctx, owner).bounds;
  death->has_owner = true;
  death->owner = owner->actor.id;
  death->solid = "trigger";
  death->movement = "none";
  death->touch = game->named_touch(game->ctx, death, QA_Q1AM_OLDNEW_PREFIX "tdeath_boss_touch");
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_angles = true;
  patch.angles = QA_Q1AM_ZERO;
  patch.has_bounds = true;
  patch.bounds.min = qa_sub3(bounds.min, (qa_vec3){1, 1, 1});
  patch.bounds.max = qa_add3(bounds.max, (qa_vec3){1, 1, 1});
  game->set_body(game->ctx, death, &patch);
  game->schedule(game->ctx, death, 0.2, game->named_action(game->ctx, death, "SUB_Remove"));
  game->link(game->ctx, death);
  game->force_retouch = 2;
  return death;
}
static void sphere_mis_touch(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                             bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  if (target && (strcmp(target->classname, "oldnew_child") == 0 ||
                 strcmp(target->classname, "oldnew_eye") == 0 ||
                 strcmp(target->classname, "monster_szombie") == 0)) {
    game->remove(game->ctx, entity);
    return;
  }
  if (target && strcmp(target->solid, "trigger") == 0) return;
  if (strcmp(game->contents(game->ctx, game->body(game->ctx, entity).origin), "sky") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (game->can_take_damage(game->ctx, other))
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 18);
  game->remove(game->ctx, entity);
}
static void ensure_enemy(qa_q1am_context *context, qa_q1am_actor *entity) {
  qa_actor_id enemy;
  if (qa_q1am_boss_enemy(context, entity, &enemy)) return;
  bool has = first_player(context->game, &enemy);
  qa_q1am_actor_set_ref(entity, "enemy", has, enemy);
}
static void actual_sphere(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  ensure_enemy(context, entity);
  qa_vec3 direction = qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles).forward;
  for (int i = 0; i < 100; i++) {
    qa_vec3 point = qa_q1am_sphere_point(i);
    qa_vec3 aim = qa_add3(qa_scale3(direction, 0.65), qa_scale3(qa_q1am_normalize(point), 0.35));
    qa_q1am_actor *shot = missile(context, entity,
                                  qa_add3(qa_add3(game->body(game->ctx, entity).origin, (qa_vec3){0, 0, 32}),
                                          qa_scale3(point, 32)),
                                  aim, "progs/rogue/sphere.mdl", qa_scale3(aim, 800));
    shot->damage = 18;
    if (i % 5 == 0) shot->effects = 64;
  }
  entity->count--;
  if (entity->count <= 0) {
    game->remove(game->ctx, entity);
    return;
  }
  qa_q1am_boss_later(context, entity, "actual_sphere", 0.8);
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_add3(game->body(game->ctx, entity).angles,
                         (qa_vec3){0, qa_q1am_actor_number(entity, "aflag") != 0 ? 20 : -20, 0});
  game->set_body(game->ctx, entity, &patch);
}
static void add_angles_y(qa_q1am_game *game, qa_q1am_actor *entity, double yaw) {
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, yaw, 0});
  game->set_body(game->ctx, entity, &patch);
}
static void spawn_sphere(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_q1am_basis initial = qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
  qa_vec3 excluded = initial.forward, excluded2 = initial.right;
  game->sound(game->ctx, entity, "weapons/spike2.wav", "weapon", 1);
  ensure_enemy(context, entity);
  for (int y = -1; y < 2; y++) {
    for (int x = 0; x < (y == 0 ? 44 : 45); x++) {
      qa_q1am_basis basis = qa_q1am_game_make_vectors(
          game, qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, x * 8.0, 0}));
      if (fabs(flat_dot(basis.forward, excluded)) >= 0.9 || fabs(flat_dot(basis.forward, excluded2)) >= 0.9)
        continue;
      qa_vec3 origin = qa_add3(qa_add3(qa_add3(game->body(game->ctx, entity).origin,
                                               qa_scale3(basis.forward, 64)),
                                       qa_scale3(basis.up, y * 16.0)),
                               (qa_vec3){0, 0, 24});
      qa_q1am_actor *shot = missile(context, entity, origin, qa_scale3(basis.forward, 200),
                                    "progs/diamond.mdl", qa_scale3(qa_q1am_normalize(basis.forward), 400));
      if (y == -1 && x % 2 == 0) shot->effects = 64;
      shot->angular_velocity = (qa_vec3){0, 0, -100};
    }
    add_angles_y(game, entity, 4);
  }
  add_angles_y(game, entity, qa_q1am_actor_number(entity, "aflag") * entity->wait);
  qa_q1am_boss_later(context, entity, "spawn_sphere", entity->delay);
  entity->count++;
  if (entity->count > qa_q1am_actor_number(entity, "cnt")) {
    qa_q1am_actor *owner = game->entity(game->ctx, entity->has_owner, entity->owner);
    if (owner && qa_q1am_actor_number(owner, "boss_immune") != 0)
      context->set_number(context->ctx, owner, "boss_immune", 0);
    game->remove(game->ctx, entity);
  }
}
static void sphere_chunk(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_vec3 delta = qa_sub3(qa_q1am_boss_target(context, entity), game->body(game->ctx, entity).origin);
  delta.z = 0;
  qa_vec3 attack = qa_q1am_normalize(delta);
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1am_velocity_angles(attack);
  game->set_body(game->ctx, entity, &patch);
  qa_vec3 excluded = qa_q1am_game_make_vectors(
      game, qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, 22.5, 0})).forward;
  qa_vec3 excluded2 = qa_q1am_game_make_vectors(
      game, qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, -22.5, 0})).forward;
  game->sound(game->ctx, entity, "weapons/spike2.wav", "weapon", 1);
  for (int y = -1; y < 1; y++) {
    for (int x = 0; x < 45; x++) {
      qa_q1am_basis basis = qa_q1am_game_make_vectors(
          game, qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, x * 8.0, 0}));
      if (flat_dot(basis.forward, attack) <= 0.5 || flat_dot(basis.forward, excluded) > 0.985 ||
          flat_dot(basis.forward, excluded2) > 0.985)
        continue;
      qa_vec3 origin = qa_add3(qa_add3(qa_add3(game->body(game->ctx, entity).origin,
                                               qa_scale3(basis.forward, 64)),
                                       qa_scale3(basis.up, y * 16.0)),
                               (qa_vec3){0, 0, 8});
      qa_q1am_actor *shot = missile(context, entity, origin, qa_scale3(basis.forward, 200),
                                    "progs/diamond.mdl", qa_scale3(qa_q1am_normalize(basis.forward), 400));
      if (y == 0 && x % 2 == 0) shot->effects = 64;
      shot->angular_velocity = (qa_vec3){0, 0, -100};
    }
    add_angles_y(game, entity, 4);
  }
  entity->count++;
  if (entity->count > qa_q1am_actor_number(entity, "cnt")) game->remove(game->ctx, entity);
}
static void tdeath_boss_touch(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                              bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  if (game->is_player(game->ctx, true, other) ||
      strcmp(game->classname_of(game->ctx, other), "monster_oldone_new") == 0) {
    qa_q1am_player *player = game->player(game->ctx, other);
    if ((player ? qa_q1am_player_powerup(player, "invulnerability") : 0) > game->time)
      (void)snprintf(entity->classname, sizeof entity->classname, "%s", "teledeath2");
    if (!game->is_player(game->ctx, entity->has_owner, entity->owner)) {
      if (entity->has_owner)
        game->damage(game->ctx, entity->owner, entity->actor.id, true, entity->actor.id, 50000);
      return;
    }
  }
  if (game->health(game->ctx, other) != 0)
    game->damage(game->ctx, other, entity->actor.id, true, entity->actor.id, 50000);
}
void qa_q1am_register_oldnew_projectiles(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_handlers handlers = {0};
  handlers.ctx = context;
  handlers.touch = sphere_mis_touch;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "sphere_mis_touch", handlers);
  handlers.touch = NULL;
  handlers.action = actual_sphere;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "actual_sphere", handlers);
  handlers.action = spawn_sphere;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "spawn_sphere", handlers);
  handlers.action = sphere_chunk;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "sphere_chunk", handlers);
  handlers.action = NULL;
  handlers.touch = tdeath_boss_touch;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "tdeath_boss_touch", handlers);
}
