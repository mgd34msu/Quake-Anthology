/* Port of src/content/q1/addons/monsters/bosses/oldnew-projectiles.ts.
 * mg3_oldone_new.qc projectiles and sphere managers. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/oldnew_projectiles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/sphere_points.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/core/math.h"
#include "../support.h"

void qa_q1am_boss_later(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double delay) {
  char full[96];
  (void)snprintf(full, sizeof full, QA_Q1AM_OLDNEW_PREFIX "%s", name);
  qa_q1_schedule(context->game, entity, delay, qa_q1_callback_registry_action(context->game->named, entity, full));
}
bool qa_q1am_boss_enemy(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id *out) {
  (void)context; /* TS bossEnemy(_context, entity) */
  if (entity->monster != NULL && entity->monster->has_enemy) {
    *out = entity->monster->enemy;
    return true;
  }
  return qa_q1_actor_reference_get(entity, "enemy", out);
}
qa_actor_id qa_q1am_boss_world(qa_q1ad_context *context) {
  if (context->game->world == NULL) qa_range_error("MG3 boss damage requires worldspawn");
  return context->game->world->actor.id;
}
qa_vec3 qa_q1am_boss_target(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_actor_id enemy;
  qa_q1_body body;
  if (!qa_q1am_boss_enemy(context, entity, &enemy) || !qa_q1am_read_body(context->game, enemy, &body)) return QA_Q1_ZERO;
  return body.origin;
}
static double flat_dot(qa_vec3 a, qa_vec3 b) {
  a.z = 0;
  b.z = 0;
  return qa_dot3(qa_q1_normalize(a), qa_q1_normalize(b));
}
static qa_q1_actor *missile(qa_q1ad_context *context, qa_q1_actor *manager, qa_vec3 origin, qa_vec3 direction,
                            const char *model, qa_vec3 velocity) {
  qa_q1_services *game = context->game;
  qa_q1_actor *shot = qa_q1_launch_spike(game, qa_q1am_opt(manager->has_owner, &manager->owner), origin, direction, "spike");
  qa_q1ad_set_text(&shot->model, model);
  shot->touch = qa_q1_callback_registry_touch(game->named, shot, QA_Q1AM_OLDNEW_PREFIX "sphere_mis_touch");
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, shot, &patch);
  return shot;
}
/* TS game.host.players()[0] ?? null. */
static const qa_actor_id *first_player(qa_q1_services *game, qa_actor_id *storage) {
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  if (count == 0) return NULL;
  *storage = players[0];
  return storage;
}
static void set_angles(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 angles) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_angles = true;
  patch.angles = angles;
  qa_q1_set_body(game, entity, &patch);
}
qa_q1_actor *qa_q1am_spawn_sphere_manager(qa_q1ad_context *context, qa_q1_actor *source, double maximum) {
  qa_q1_services *game = context->game;
  qa_q1_actor *manager = qa_q1_create(game, "sphere_manager", NULL, NULL);
  qa_q1ad_set_text(&manager->classname, "");
  manager->has_owner = true;
  manager->owner = source->actor.id;
  qa_q1_set_origin(game, manager, qa_q1_get_body(game, source).origin);
  qa_actor_id enemy;
  if (!qa_q1am_boss_enemy(context, source, &enemy)) {
    qa_actor_id storage;
    const qa_actor_id *player = first_player(game, &storage);
    if (source->monster != NULL) {
      source->monster->has_enemy = player != NULL;
      if (player != NULL) source->monster->enemy = *player;
    } else {
      qa_q1am_reference(source, "enemy", player);
    }
  }
  qa_vec3 a = qa_q1am_boss_target(context, source);
  qa_vec3 b = qa_q1_get_body(game, source).origin;
  qa_vec3 delta = qa_sub3(a, b);
  delta.z = 0;
  const qa_vec3 direction = qa_q1_normalize(delta);
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, source).angles);
  qa_q1ad_set_number(context, manager, "aflag", flat_dot(basis.right, qa_cross3_double(direction, (qa_vec3){0, 0, 1})) > 1 ? 1 : 0);
  set_angles(game, manager, qa_q1mp_velocity_angles(direction));
  manager->count = maximum;
  qa_q1am_boss_later(context, manager, "actual_sphere", 0.1);
  return manager;
}
qa_q1_actor *qa_q1am_spawn_sphere_chunk_manager(qa_q1ad_context *context, qa_q1_actor *source, double maximum) {
  qa_q1_services *game = context->game;
  qa_q1_actor *manager = qa_q1_create(game, "sphere_chunk_manager", NULL, NULL);
  qa_q1ad_set_text(&manager->classname, "");
  manager->has_owner = true;
  manager->owner = source->actor.id;
  manager->wait = 22.5;
  manager->delay = 0.8;
  manager->count = 0;
  qa_q1ad_set_number(context, manager, "aflag", 1);
  qa_q1ad_set_number(context, manager, "cnt", maximum);
  qa_actor_id enemy;
  const qa_actor_id *target = qa_q1am_boss_enemy(context, source, &enemy) ? &enemy : first_player(game, &enemy);
  qa_q1am_reference(manager, "enemy", target);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_q1_get_body(game, source).origin;
  patch.has_angles = true;
  patch.angles = qa_q1_get_body(game, source).angles;
  qa_q1_set_body(game, manager, &patch);
  qa_q1am_boss_later(context, manager, "sphere_chunk", 0.1);
  return manager;
}
void qa_q1am_auto_gun(qa_q1ad_context *context, qa_q1_actor *source, qa_vec3 origin, double offset) {
  qa_q1_services *game = context->game;
  qa_actor_id storage;
  const qa_actor_id *player = first_player(game, &storage);
  qa_q1_body body;
  const qa_vec3 target = player != NULL && qa_q1am_read_body(game, *player, &body) ? body.origin : QA_Q1_ZERO;
  qa_vec3 direction = qa_q1_normalize(qa_sub3(target, origin));
  if (offset != 0)
    direction = qa_add3(qa_scale3(qa_cross3_double(direction, (qa_vec3){0, 0, 1}), offset), qa_scale3(direction, 1 - fabs(offset)));
  qa_q1_actor *shot = qa_q1_launch_spike(game, &source->actor.id, origin, direction, "spike");
  shot->touch = qa_q1_callback_registry_touch(game->named, shot, QA_Q1AM_OLDNEW_PREFIX "sphere_mis_touch");
  qa_q1ad_set_text(&shot->model, "progs/rogue/sphere.mdl");
  shot->effects = 64;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, offset == 0 ? 600 : 800);
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, shot, &patch);
}
qa_q1_actor *qa_q1am_spawn_boss_teledeath(qa_q1ad_context *context, qa_vec3 origin, qa_q1_actor *owner) {
  qa_q1_services *game = context->game;
  qa_q1_actor *death = qa_q1_create(game, "teledeath", NULL, NULL);
  const qa_bounds bounds = qa_q1_get_body(game, owner).bounds;
  death->has_owner = true;
  death->owner = owner->actor.id;
  death->solid = "trigger";
  death->movement = "none";
  death->touch = qa_q1_callback_registry_touch(game->named, death, QA_Q1AM_OLDNEW_PREFIX "tdeath_boss_touch");
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_angles = true;
  patch.angles = QA_Q1_ZERO;
  patch.has_bounds = true;
  patch.bounds.min = qa_sub3(bounds.min, (qa_vec3){1, 1, 1});
  patch.bounds.max = qa_add3(bounds.max, (qa_vec3){1, 1, 1});
  qa_q1_set_body(game, death, &patch);
  qa_q1_schedule(game, death, 0.2, qa_q1_callback_registry_action(game->named, death, "SUB_Remove"));
  qa_q1_link(game, death);
  game->force_retouch = 2;
  return death;
}
static bool classname_is(const qa_q1_actor *entity, const char *classname) {
  return entity != NULL && strcmp(entity->classname, classname) == 0;
}
static void sphere_mis_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                             const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (_game, entity, other) */
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if (classname_is(target, "oldnew_child") || classname_is(target, "oldnew_eye") || classname_is(target, "monster_szombie")) {
    qa_q1_remove(game, entity);
    return;
  }
  if (target != NULL && strcmp(target->solid, "trigger") == 0) return;
  if (strcmp(game->host.contents(game->host.ctx, qa_q1_get_body(game, entity).origin), "sky") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  if (qa_q1am_can_take_damage(game, other))
    (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 18, NULL, NULL, NULL, NULL);
  qa_q1_remove(game, entity);
}
static void actual_sphere(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_actor_id enemy;
  if (!qa_q1am_boss_enemy(context, entity, &enemy)) qa_q1am_reference(entity, "enemy", first_player(game, &enemy));
  const qa_vec3 direction = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward;
  for (int i = 0; i < 100; i++) {
    const qa_vec3 point = qa_q1am_sphere_point(i);
    const qa_vec3 aim = qa_add3(qa_scale3(direction, 0.65), qa_scale3(qa_q1_normalize(point), 0.35));
    qa_q1_actor *shot = missile(context, entity, qa_add3(qa_add3(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, 32}), qa_scale3(point, 32)),
                                aim, "progs/rogue/sphere.mdl", qa_scale3(aim, 800));
    shot->damage = 18;
    if (i % 5 == 0) shot->effects = 64;
  }
  entity->count--;
  if (entity->count <= 0) { qa_q1_remove(game, entity); return; }
  qa_q1am_boss_later(context, entity, "actual_sphere", 0.8);
  qa_vec3 a = qa_q1_get_body(game, entity).angles;
  qa_vec3 b = (qa_vec3){0, qa_q1_actor_number(entity, "aflag") != 0 ? 20 : -20, 0};
  set_angles(game, entity, qa_add3(a, b));
}
static void spawn_sphere(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  const qa_q1_basis initial = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  const qa_vec3 excluded = initial.forward, excluded2 = initial.right;
  qa_q1_sound(game, entity->actor.id, "weapons/spike2.wav", "weapon", 1, 1);
  qa_actor_id enemy;
  if (!qa_q1am_boss_enemy(context, entity, &enemy)) qa_q1am_reference(entity, "enemy", first_player(game, &enemy));
  for (int y = -1; y < 2; y++) {
    for (int x = 0; x < (y == 0 ? 44 : 45); x++) {
      const qa_q1_basis basis = qa_q1_make_vectors(game, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, x * 8.0, 0}));
      if (fabs(flat_dot(basis.forward, excluded)) >= 0.9 || fabs(flat_dot(basis.forward, excluded2)) >= 0.9) continue;
      const qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1_get_body(game, entity).origin, qa_scale3(basis.forward, 64)),
                                             qa_scale3(basis.up, y * 16.0)), (qa_vec3){0, 0, 24});
      qa_q1_actor *shot = missile(context, entity, origin, qa_scale3(basis.forward, 200), "progs/diamond.mdl",
                                  qa_scale3(qa_q1_normalize(basis.forward), 400));
      if (y == -1 && x % 2 == 0) shot->effects = 64;
      shot->angular_velocity = (qa_vec3){0, 0, -100};
    }
    set_angles(game, entity, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, 4, 0}));
  }
  qa_vec3 a = qa_q1_get_body(game, entity).angles;
  qa_vec3 b = (qa_vec3){0, qa_q1_actor_number(entity, "aflag") * entity->wait, 0};
  set_angles(game, entity, qa_add3(a, b));
  qa_q1am_boss_later(context, entity, "spawn_sphere", entity->delay);
  entity->count++;
  if (entity->count > qa_q1_actor_number(entity, "cnt")) {
    qa_q1_actor *owner = qa_q1_get_entity(game, qa_q1am_opt(entity->has_owner, &entity->owner));
    if (owner != NULL && qa_q1_actor_number(owner, "boss_immune") != 0) qa_q1ad_set_number(context, owner, "boss_immune", 0);
    qa_q1_remove(game, entity);
  }
}
static void sphere_chunk(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_vec3 a = qa_q1am_boss_target(context, entity);
  qa_vec3 b = qa_q1_get_body(game, entity).origin;
  qa_vec3 delta = qa_sub3(a, b);
  delta.z = 0;
  const qa_vec3 attack = qa_q1_normalize(delta);
  set_angles(game, entity, qa_q1mp_velocity_angles(attack));
  const qa_vec3 excluded = qa_q1_make_vectors(game, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, 22.5, 0})).forward;
  const qa_vec3 excluded2 = qa_q1_make_vectors(game, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, -22.5, 0})).forward;
  qa_q1_sound(game, entity->actor.id, "weapons/spike2.wav", "weapon", 1, 1);
  for (int y = -1; y < 1; y++) {
    for (int x = 0; x < 45; x++) {
      const qa_q1_basis basis = qa_q1_make_vectors(game, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, x * 8.0, 0}));
      if (flat_dot(basis.forward, attack) <= 0.5 || flat_dot(basis.forward, excluded) > 0.985 ||
          flat_dot(basis.forward, excluded2) > 0.985)
        continue;
      const qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1_get_body(game, entity).origin, qa_scale3(basis.forward, 64)),
                                             qa_scale3(basis.up, y * 16.0)), (qa_vec3){0, 0, 8});
      qa_q1_actor *shot = missile(context, entity, origin, qa_scale3(basis.forward, 200), "progs/diamond.mdl",
                                  qa_scale3(qa_q1_normalize(basis.forward), 400));
      if (y == 0 && x % 2 == 0) shot->effects = 64;
      shot->angular_velocity = (qa_vec3){0, 0, -100};
    }
    set_angles(game, entity, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, 4, 0}));
  }
  entity->count++;
  if (entity->count > qa_q1_actor_number(entity, "cnt")) qa_q1_remove(game, entity);
}
static void tdeath_boss_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                              const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (_game, entity, other) */
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const qa_actor_id self = entity->actor.id;
  if (qa_q1_is_player(game, &other) || strcmp(game->host.classname(game->host.ctx, other), "monster_oldone_new") == 0) {
    const qa_q1_player_state *player = qa_q1_player(game, other);
    if ((player != NULL ? qa_q1_player_powerup_get(player, "invulnerability") : 0) > game->time)
      qa_q1ad_set_text(&entity->classname, "teledeath2");
    if (!qa_q1_is_player(game, qa_q1am_opt(entity->has_owner, &entity->owner))) {
      if (entity->has_owner) (void)qa_q1_damage(game, entity->owner, self, &self, 50000, NULL, NULL, NULL, NULL);
      return;
    }
  }
  if (qa_q1_health(game, other) != 0) (void)qa_q1_damage(game, other, self, &self, 50000, NULL, NULL, NULL, NULL);
}
void qa_q1am_register_oldnew_projectiles(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.touch = sphere_mis_touch;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_OLDNEW_PREFIX "sphere_mis_touch", handlers);
  handlers.touch = NULL;
  handlers.action = actual_sphere;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_OLDNEW_PREFIX "actual_sphere", handlers);
  handlers.action = spawn_sphere;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_OLDNEW_PREFIX "spawn_sphere", handlers);
  handlers.action = sphere_chunk;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_OLDNEW_PREFIX "sphere_chunk", handlers);
  handlers.action = NULL;
  handlers.touch = tdeath_boss_touch;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_OLDNEW_PREFIX "tdeath_boss_touch", handlers);
}
