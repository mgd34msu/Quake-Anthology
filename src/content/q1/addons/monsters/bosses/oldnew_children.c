/* Port of src/content/q1/addons/monsters/bosses/oldnew-children.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/oldnew_children.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/oldnew_projectiles.h"

#define FLAGS(x) ((int32_t)(x))

static const qa_bounds large_hull = {{-32, -32, -24}, {32, 32, 64}};
static qa_q1am_actor *child(qa_q1am_context *context, qa_q1am_actor *owner) {
  qa_q1am_actor *entity = context->game->create(context->game->ctx, "oldnew_child");
  entity->has_owner = true;
  entity->owner = owner->actor.id;
  context->game->set_origin(context->game->ctx, entity, context->game->body(context->game->ctx, owner).origin);
  return entity;
}
static bool first_player(qa_q1am_game *game, qa_actor_id *out) {
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  bool has = count > 0;
  if (has) *out = players[0];
  free(players);
  return has;
}
qa_q1am_actor *qa_q1am_spawn_spammer(qa_q1am_context *context, qa_q1am_actor *owner) {
  qa_q1am_actor *entity = child(context, owner);
  qa_actor_id enemy;
  bool has = qa_q1am_boss_enemy(context, owner, &enemy);
  if (!has) has = first_player(context->game, &enemy);
  qa_q1am_actor_set_ref(entity, "enemy", has, enemy);
  qa_q1am_boss_later(context, entity, "spammer_think", 0.1);
  return entity;
}
qa_q1am_actor *qa_q1am_spawn_swiper(qa_q1am_context *context, qa_q1am_actor *owner) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *entity = child(context, owner);
  entity->count = 0;
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = game->body(game->ctx, owner).angles;
  game->set_body(game->ctx, entity, &patch);
  game->sound(game->ctx, entity, "weapons/lstart.wav", "weapon", 1);
  context->set_number(context->ctx, entity, "aflag", qa_q1am_actor_number(owner, "aflag"));
  context->set_number(context->ctx, owner, "aflag", 1 - qa_q1am_actor_number(owner, "aflag"));
  qa_q1am_boss_later(context, entity, "oldnew_swipe", 0.025);
  return entity;
}
qa_q1am_actor *qa_q1am_spawn_eye(qa_q1am_context *context, qa_q1am_actor *owner) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *entity = game->create(game->ctx, "oldnew_eye");
  qa_vec3 origin = qa_add3(game->body(game->ctx, owner).origin, (qa_vec3){0, 0, 300});
  game->spawn_teleport_fog(game->ctx, game, origin);
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_bounds = true;
  patch.bounds = large_hull;
  game->set_body(game->ctx, entity, &patch);
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/teleporter_eye.mdl");
  entity->solid = "slidebox";
  entity->movement = "fly";
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  entity->pain = game->named_pain(game->ctx, entity, QA_Q1AM_OLDNEW_PREFIX "eye_pain");
  entity->die = game->named_die(game->ctx, entity, QA_Q1AM_OLDNEW_PREFIX "eye_die");
  entity->touch = game->named_touch(game->ctx, entity, QA_Q1AM_OLDNEW_PREFIX "eye_touch");
  entity->has_owner = true;
  entity->owner = owner->actor.id;
  entity->speed = 100;
  game->combat_set_health(game->ctx, entity->actor, 300);
  entity->max_health = 300;
  qa_actor_id player;
  bool has = first_player(game, &player);
  qa_q1am_actor_set_ref(entity, "enemy", has, player);
  qa_q1am_boss_later(context, entity, "eye_chase", 0.1);
  game->link(game->ctx, entity);
  return entity;
}
static qa_q1am_actor *armed_eye(qa_q1am_context *context, qa_q1am_actor *owner, double offset) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *entity = child(context, owner);
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, game->body(game->ctx, owner).angles);
  qa_vec3 origin = qa_add3(qa_add3(qa_add3(game->body(game->ctx, owner).origin,
                                           qa_scale3(basis.right, offset * 300)),
                                   qa_scale3(basis.up, 80)),
                           qa_scale3(basis.forward, 200));
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_bounds = true;
  patch.bounds = large_hull;
  game->set_body(game->ctx, entity, &patch);
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/teleporter_eye.mdl");
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->movement_flags = 33;
  entity->die = game->named_die(game->ctx, entity, QA_Q1AM_OLDNEW_PREFIX "eye_die");
  qa_q1am_spawn_boss_teledeath(context, origin, entity);
  game->combat_set_health(game->ctx, entity->actor, 120);
  entity->max_health = 120;
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  game->total_monsters++;
  qa_q1am_event event = {0};
  event.kind = "monster-total";
  event.total = game->total_monsters;
  game->emit(game->ctx, &event);
  patch.has_origin = false;
  patch.has_bounds = false;
  patch.has_angles = true;
  patch.angles = game->body(game->ctx, owner).angles;
  game->set_body(game->ctx, entity, &patch);
  game->spawn_teleport_fog(game->ctx, game, origin);
  entity->effects = 64;
  game->link(game->ctx, entity);
  return entity;
}
void qa_q1am_spawn_blaster(qa_q1am_context *context, qa_q1am_actor *owner) {
  const double offsets[] = {1, -1};
  for (int i = 0; i < 2; i++) {
    qa_q1am_actor *entity = armed_eye(context, owner, offsets[i]);
    context->set_number(context->ctx, entity, "aflag", offsets[i]);
    entity->count = 0;
    entity->damage = -offsets[i];
    qa_q1am_boss_later(context, entity, "blast", 2);
  }
}
qa_q1am_actor *qa_q1am_spawn_vortex(qa_q1am_context *context, qa_q1am_actor *owner) {
  double side = qa_q1am_actor_number(owner, "ammo_cells") != 0 ? 1 : -1;
  qa_q1am_actor *entity = armed_eye(context, owner, side);
  context->set_number(context->ctx, entity, "aflag", side);
  context->set_number(context->ctx, owner, "ammo_cells", 1 - qa_q1am_actor_number(owner, "ammo_cells"));
  entity->count = 0;
  qa_actor_id enemy;
  bool has = qa_q1am_boss_enemy(context, owner, &enemy);
  qa_q1am_actor_set_ref(entity, "enemy", has, enemy);
  qa_q1am_boss_later(context, entity, "vortex_think_alt", 2);
  return entity;
}
void qa_q1am_cleanup_oldnew(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  size_t total = game->entity_count(game->ctx);
  for (size_t i = 0; i < total; i++) {
    qa_q1am_actor *entity = game->entity_at(game->ctx, i);
    if (entity && (strcmp(entity->classname, "oldnew_child") == 0 ||
                   strcmp(entity->classname, "oldnew_eye") == 0))
      qa_q1am_boss_later(context, entity, "oldnew_cleanup_think", 0.1);
  }
  qa_q1am_actor *timer = game->create(game->ctx, "oldnew_cleanup_zombies");
  timer->classname[0] = '\0';
  qa_q1am_boss_later(context, timer, "oldnew_cleanup_zombies", 2);
}
static void eye_die(qa_q1am_context *context, qa_q1am_actor *entity, bool has_attacker, qa_actor_id attacker) {
  qa_q1am_game *game = context->game;
  if ((FLAGS(entity->movement_flags) & 32) != 0) {
    game->killed_monsters++;
    qa_q1am_event event = {0};
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    game->emit(game->ctx, &event);
    entity->movement_flags = (double)(FLAGS(entity->movement_flags) & ~3);
    qa_q1am_actor_set_ref(entity, "enemy", has_attacker, attacker);
    game->use_targets(game->ctx, entity, has_attacker, attacker);
  }
  qa_q1am_event event = {0};
  event.kind = "colored-explosion";
  event.origin = game->body(game->ctx, entity).origin;
  event.color_start = 244;
  event.color_length = 3;
  context->services_emit(context->services_ctx, &event);
  game->remove(game->ctx, entity);
}
static void eye_die_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has, qa_actor_id attacker) {
  (void)game;
  eye_die(ctx, entity, has, attacker);
}
static void eye_pain_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has,
                             qa_actor_id attacker, double take) {
  (void)ctx;
  qa_vec3 origin = game->body(game->ctx, entity).origin;
  qa_q1am_body attacker_body;
  qa_vec3 attacker_origin = has && game->body_read(game->ctx, attacker, &attacker_body)
                                ? attacker_body.origin : QA_Q1AM_ZERO;
  qa_vec3 delta = qa_sub3(origin, attacker_origin);
  delta.z = 0;
  qa_vec3 impulse = qa_scale3(qa_add3(qa_scale3(qa_q1am_normalize(delta), 0.8), (qa_vec3){0, 0, 0.2}), take * 10);
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_add3(game->body(game->ctx, entity).velocity, impulse);
  game->set_body(game->ctx, entity, &patch);
}
static void eye_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                              bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (game->entity(game->ctx, true, other) == game->world ||
      (entity->has_owner && qa_same_actor(entity->owner, other)))
    return;
  if (game->health(game->ctx, other) != 0)
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 500);
}
static void eye_chase_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_q1am_body body = game->body(game->ctx, entity);
  qa_vec3 target = qa_q1am_boss_target(context, entity);
  entity->speed = qa_length3(body.velocity);
  if (entity->speed < 300) entity->speed += 10;
  qa_vec3 velocity = qa_add3(qa_scale3(qa_q1am_normalize(qa_sub3(target, body.origin)), 0.3),
                             qa_scale3(qa_q1am_normalize(body.velocity), 0.7));
  if (body.origin.z < target.z + 32) {
    velocity.z = 0;
    velocity = qa_q1am_normalize(velocity);
  }
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1am_velocity_angles(velocity);
  patch.has_velocity = true;
  patch.velocity = qa_scale3(velocity, entity->speed);
  game->set_body(game->ctx, entity, &patch);
  qa_q1am_boss_later(context, entity, "eye_chase", 0.1);
  if ((int)entity->count % 2 == 0) {
    entity->effects = (double)((int32_t)entity->effects | 2);
    game->sound(game->ctx, entity, "misc/power.wav", "voice", 1);
  }
  entity->count++;
}
static void blast(qa_q1am_context *context, qa_q1am_actor *entity, const char *name) {
  qa_q1am_game *game = context->game;
  qa_actor_id enemy;
  bool has_enemy = qa_q1am_boss_enemy(context, entity, &enemy);
  if (!has_enemy || !game->is_player(game->ctx, true, enemy)) {
    bool has = first_player(game, &enemy);
    qa_q1am_actor_set_ref(entity, "enemy", has, enemy);
  }
  qa_q1am_body body = game->body(game->ctx, entity);
  qa_vec3 target = qa_q1am_boss_target(context, entity);
  qa_vec3 direction = qa_q1am_normalize(qa_sub3(target, body.origin));
  direction = qa_q1am_normalize(qa_add3(
      direction, qa_scale3(qa_cross3_double(direction, (qa_vec3){0, 0, 1}),
                           cos(entity->count * 15) * 0.5 * qa_q1am_actor_number(entity, "aflag"))));
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1am_velocity_angles(direction);
  game->set_body(game->ctx, entity, &patch);
  qa_q1am_actor *shot = game->launch_spike(game->ctx, game, true, entity->actor.id,
                                           qa_add3(body.origin, qa_scale3(direction, 8)), direction);
  shot->touch = game->named_touch(game->ctx, shot, QA_Q1AM_OLDNEW_PREFIX "blast_touch");
  (void)snprintf(shot->model, sizeof shot->model, "%s", "progs/rogue/sphere.mdl");
  shot->effects = 64;
  game->sound(game->ctx, entity, "weapons/spike2.wav", "weapon", 1);
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, 500);
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  game->set_body(game->ctx, shot, &patch);
  shot->angular_velocity = qa_scale3((qa_vec3){300, 300, 300}, game->random(game->ctx) * 2 - 1);
  qa_q1am_boss_later(context, entity, name, 0.2);
  entity->count++;
  if (entity->count > 72) {
    qa_actor_id world = qa_q1am_boss_world(context);
    game->damage(game->ctx, entity->actor.id, world, true, world, 500);
  }
}
static void blast_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  blast(ctx, entity, "blast");
}
static void vortex_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  blast(ctx, entity, "vortex_think_alt");
}
static void blast_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                                bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (game->entity(game->ctx, true, other) == game->world) {
    game->remove(game->ctx, entity);
    return;
  }
  if (strcmp(game->classname_of(game->ctx, other), "sphere") == 0 ||
      (entity->has_owner && qa_same_actor(entity->owner, other)))
    return;
  if (strcmp(game->classname_of(game->ctx, other), "monster_oldone_new") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  if (target && strcmp(target->solid, "trigger") == 0) return;
  if (game->health(game->ctx, other) != 0)
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 15);
  game->remove(game->ctx, entity);
}
static void spammer_think_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_q1am_actor *owner = game->entity(game->ctx, entity->has_owner, entity->owner);
  qa_vec3 angles = owner ? game->body(game->ctx, owner).angles : QA_Q1AM_ZERO;
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = angles;
  game->set_body(game->ctx, entity, &patch);
  static const double offsets[] = {0, 1, -3, 2, -2, 3, -1, 4, 2, -1, -3, 2, -4};
  if (entity->count < 0 || entity->count >= 13) {
    game->remove(game->ctx, entity);
    return;
  }
  double offset = offsets[(int)entity->count];
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, qa_add3(angles, (qa_vec3){0, offset * 6, 0}));
  qa_q1am_actor *shot = game->create(game->ctx, "spam");
  qa_vec3 origin = qa_add3(qa_add3(game->body(game->ctx, entity).origin, (qa_vec3){0, 0, 50}),
                           qa_scale3(basis.forward, 48));
  shot->has_owner = entity->has_owner;
  shot->owner = entity->owner;
  (void)snprintf(shot->model, sizeof shot->model, "%s", "progs/rogue/plasma.mdl");
  shot->solid = "bbox";
  shot->movement = "toss";
  shot->effects = 64;
  shot->touch = game->named_touch(game->ctx, shot, QA_Q1AM_OLDNEW_PREFIX "spam_touch");
  double oomph = fmax(200, qa_length3(qa_sub3(qa_q1am_boss_target(context, entity),
                                              game->body(game->ctx, entity).origin)) * 0.8);
  qa_vec3 velocity = qa_scale3(basis.forward, oomph + 25 * entity->count);
  velocity.z = 200;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  patch.has_velocity = true;
  patch.velocity = velocity;
  game->set_body(game->ctx, shot, &patch);
  game->sound(game->ctx, entity, "weapons/grenade.wav", "weapon", 1);
  entity->count++;
  game->link(game->ctx, shot);
  qa_q1am_boss_later(context, entity, "spammer_think", 0.1);
}
static void spam_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                               bool has_normal, qa_vec3 normal) {
  qa_q1am_context *context = ctx;
  (void)has_normal; (void)normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  if (game->entity(game->ctx, true, other) == game->world) {
    qa_q1am_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = QA_Q1AM_ZERO;
    patch.has_angles = true;
    patch.angles = (qa_vec3){0, game->random(game->ctx) * 360, 0};
    game->set_body(game->ctx, entity, &patch);
    entity->movement = "none";
    entity->solid = "none";
    (void)snprintf(entity->model, sizeof entity->model, "%s", "maps/bmodel/b_splash.bsp");
    entity->touch.present = false;
    game->link(game->ctx, entity);
    qa_q1am_boss_later(context, entity, "spam1", 2);
    return;
  }
  if (game->health(game->ctx, other) != 0) {
    qa_actor_id inflictor = entity->has_owner ? entity->owner : qa_q1am_boss_world(context);
    game->damage(game->ctx, other, inflictor, true, entity->actor.id, 10);
  }
  game->remove(game->ctx, entity);
}
static void spam1_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_vec3 origin = game->body(game->ctx, entity).origin;
  qa_q1am_event event = {0};
  event.kind = "lightning";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.style = 1;
  event.start = origin;
  event.end = qa_add3(origin, (qa_vec3){0, 0, 500});
  context->services_emit(context->services_ctx, &event);
  qa_q1am_boss_later(context, entity, "spam2", 0.1);
}
static void spam2_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_q1am_event event = {0};
  event.kind = "colored-explosion";
  event.origin = game->body(game->ctx, entity).origin;
  event.color_start = 244;
  event.color_length = 3;
  context->services_emit(context->services_ctx, &event);
  game->radius_damage(game->ctx, entity->actor.id, true, entity->actor.id, 100,
                      game->world != NULL, game->world ? game->world->actor.id : (qa_actor_id){0}, NULL);
  game->remove(game->ctx, entity);
}
static void swipe_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  double a = pow(entity->count / 50, 2);
  qa_q1am_basis basis = qa_q1am_game_make_vectors(
      game, qa_add3(game->body(game->ctx, entity).angles,
                    (qa_vec3){0, qa_q1am_actor_number(entity, "aflag") == 0 ? -60 + a * 180 : 60 - a * 180, 0}));
  qa_vec3 origin = game->body(game->ctx, entity).origin;
  qa_vec3 start = qa_add3(origin, qa_scale3(basis.forward, 130));
  qa_q1am_trace_request request = {start, qa_add3(origin, qa_scale3(basis.forward, 1000)),
                                   QA_Q1AM_POINT, true, entity->actor.id, true};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  if (trace.has_actor && game->health(game->ctx, trace.actor) != 0) {
    qa_actor_id inflictor = entity->has_owner ? entity->owner : qa_q1am_boss_world(context);
    game->damage(game->ctx, trace.actor, inflictor, entity->has_owner, entity->owner,
                 strcmp(game->classname_of(game->ctx, trace.actor), "monster_szombie") == 0 ? 100 : 25);
  }
  if ((int)entity->count % 2 == 0) {
    qa_q1am_event event = {0};
    event.kind = "lightning";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.style = 3;
    event.start = start;
    event.end = trace.end;
    context->services_emit(context->services_ctx, &event);
  }
  if (qa_q1am_actor_number(entity, "t_width") < game->time) {
    game->sound(game->ctx, entity, "weapons/lhit.wav", "weapon", 1);
    context->set_number(context->ctx, entity, "t_width", game->time + 0.6);
  }
  entity->count++;
  if (entity->count > 50)
    game->schedule(game->ctx, entity, 0.025, game->named_action(game->ctx, entity, "SUB_Remove"));
  else
    qa_q1am_boss_later(context, entity, "oldnew_swipe", 0.025);
}
static void cleanup_think_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx;
  game->damage(game->ctx, entity->actor.id, entity->actor.id, true, entity->actor.id, 5000);
}
static void cleanup_zombies_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_q1am_actor *zombie = NULL;
  size_t total = game->entity_count(game->ctx);
  for (size_t i = 0; i < total && zombie == NULL; i++) {
    qa_q1am_actor *candidate = game->entity_at(game->ctx, i);
    if (candidate && strcmp(candidate->classname, "monster_szombie") == 0) zombie = candidate;
  }
  if (zombie) {
    qa_actor_id world = qa_q1am_boss_world(context);
    game->damage(game->ctx, zombie->actor.id, world, true, world, 500);
    qa_q1am_boss_later(context, entity, "oldnew_cleanup_zombies", 0.2 + game->random(game->ctx) * 0.5);
    return;
  }
  qa_q1am_boss_later(context, entity, "oldnew_cleanup_zombies", 1.5);
}
void qa_q1am_register_oldnew_children(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_handlers handlers = {0};
  handlers.ctx = context;
  handlers.die = eye_die_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "eye_die", handlers);
  handlers.die = NULL;
  handlers.pain = eye_pain_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "eye_pain", handlers);
  handlers.pain = NULL;
  handlers.touch = eye_touch_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "eye_touch", handlers);
  handlers.touch = NULL;
  handlers.action = eye_chase_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "eye_chase", handlers);
  handlers.action = blast_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "blast", handlers);
  handlers.action = vortex_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "vortex_think_alt", handlers);
  handlers.action = NULL;
  handlers.touch = blast_touch_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "blast_touch", handlers);
  handlers.touch = NULL;
  handlers.action = spammer_think_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "spammer_think", handlers);
  handlers.action = NULL;
  handlers.touch = spam_touch_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "spam_touch", handlers);
  handlers.touch = NULL;
  handlers.action = spam1_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "spam1", handlers);
  handlers.action = spam2_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "spam2", handlers);
  handlers.action = swipe_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "oldnew_swipe", handlers);
  handlers.action = cleanup_think_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "oldnew_cleanup_think", handlers);
  handlers.action = cleanup_zombies_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "oldnew_cleanup_zombies", handlers);
}
