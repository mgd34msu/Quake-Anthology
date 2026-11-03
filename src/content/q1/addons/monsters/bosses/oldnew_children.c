/* Port of src/content/q1/addons/monsters/bosses/oldnew-children.ts.
 * mg3_oldone_new.qc summoned attack actors. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/oldnew_children.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/oldnew_projectiles.h"
#include "qa/content/q1/addons/qc_math.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/core/math.h"
#include "../support.h"

#define PREFIX QA_Q1AM_OLDNEW_PREFIX
#define FLAGS QA_Q1AM_FLAGS

static const qa_bounds large_hull = {{-32, -32, -24}, {32, 32, 64}};

static const qa_actor_id *first_player(qa_q1_services *game, qa_actor_id *storage) {
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  if (count == 0) return NULL;
  *storage = players[0];
  return storage;
}
static qa_q1_actor *child(qa_q1ad_context *context, qa_q1_actor *owner) {
  qa_q1_actor *entity = qa_q1_create(context->game, "oldnew_child", NULL, NULL);
  entity->has_owner = true;
  entity->owner = owner->actor.id;
  qa_q1_set_origin(context->game, entity, qa_q1_get_body(context->game, owner).origin);
  return entity;
}
static void set_angles(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 angles) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_angles = true;
  patch.angles = angles;
  qa_q1_set_body(game, entity, &patch);
}
static void set_origin_bounds(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 origin, qa_bounds bounds) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_bounds = true;
  patch.bounds = bounds;
  qa_q1_set_body(game, entity, &patch);
}
qa_q1_actor *qa_q1am_spawn_spammer(qa_q1ad_context *context, qa_q1_actor *owner) {
  qa_q1_actor *entity = child(context, owner);
  qa_actor_id enemy;
  const qa_actor_id *target = qa_q1am_boss_enemy(context, owner, &enemy) ? &enemy : first_player(context->game, &enemy);
  qa_q1am_reference(entity, "enemy", target);
  qa_q1am_boss_later(context, entity, "spammer_think", 0.1);
  return entity;
}
qa_q1_actor *qa_q1am_spawn_swiper(qa_q1ad_context *context, qa_q1_actor *owner) {
  qa_q1_services *game = context->game;
  qa_q1_actor *entity = child(context, owner);
  entity->count = 0;
  set_angles(game, entity, qa_q1_get_body(game, owner).angles);
  qa_q1_sound(game, owner->actor.id, "weapons/lstart.wav", "auto", 1, 1);
  qa_q1ad_set_number(context, entity, "aflag", qa_q1_actor_number(owner, "aflag"));
  qa_q1ad_set_number(context, owner, "aflag", 1 - qa_q1_actor_number(owner, "aflag"));
  qa_q1am_boss_later(context, entity, "oldnew_swipe", 0.025);
  return entity;
}
qa_q1_actor *qa_q1am_spawn_eye(qa_q1ad_context *context, qa_q1_actor *owner) {
  qa_q1_services *game = context->game;
  qa_q1_actor *entity = qa_q1_create(game, "oldnew_eye", NULL, NULL);
  const qa_vec3 origin = qa_add3(qa_q1_get_body(game, owner).origin, (qa_vec3){0, 0, 300});
  (void)qa_q1_spawn_teleport_fog(game, origin);
  set_origin_bounds(game, entity, origin, large_hull);
  qa_q1ad_set_text(&entity->model, "progs/teleporter_eye.mdl");
  entity->solid = "slidebox";
  entity->movement = "fly";
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX "eye_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX "eye_die");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, PREFIX "eye_touch");
  entity->has_owner = true;
  entity->owner = owner->actor.id;
  entity->speed = 100;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 300);
  entity->max_health = 300;
  qa_actor_id player;
  qa_q1am_reference(entity, "enemy", first_player(game, &player));
  qa_q1am_boss_later(context, entity, "eye_chase", 0.1);
  qa_q1_link(game, entity);
  return entity;
}
static qa_q1_actor *armed_eye(qa_q1ad_context *context, qa_q1_actor *owner, double offset) {
  qa_q1_services *game = context->game;
  qa_q1_actor *entity = child(context, owner);
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, owner).angles);
  const qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1_get_body(game, owner).origin, qa_scale3(basis.right, offset * 300)),
                                         qa_scale3(basis.up, 80)), qa_scale3(basis.forward, 200));
  set_origin_bounds(game, entity, origin, large_hull);
  qa_q1ad_set_text(&entity->model, "progs/teleporter_eye.mdl");
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->movement_flags = 33;
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX "eye_die");
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX "armed_eye_pain");
  (void)qa_q1am_spawn_boss_teledeath(context, origin, entity);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 120);
  entity->max_health = 120;
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  game->total_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-total";
  event.total = game->total_monsters;
  qa_q1_emit(game, event);
  set_angles(game, entity, qa_q1_get_body(game, owner).angles);
  (void)qa_q1_spawn_teleport_fog(game, origin);
  entity->effects = 64;
  qa_q1_link(game, entity);
  return entity;
}
void qa_q1am_spawn_blaster(qa_q1ad_context *context, qa_q1_actor *owner) {
  static const double offsets[2] = {1, -1};
  for (size_t i = 0; i < 2; i++) {
    const double offset = offsets[i];
    qa_q1_actor *entity = armed_eye(context, owner, offset);
    qa_q1ad_set_number(context, entity, "aflag", offset);
    entity->count = 0;
    entity->damage = -offset;
    qa_q1am_field_text(entity, "oldnew.armedThink", "blast");
    qa_q1am_boss_later(context, entity, "blast", 2);
  }
}
qa_q1_actor *qa_q1am_spawn_vortex(qa_q1ad_context *context, qa_q1_actor *owner) {
  qa_q1_actor *entity = armed_eye(context, owner, qa_q1_actor_number(owner, "ammo_cells") != 0 ? 1 : -1);
  qa_q1ad_set_number(context, entity, "aflag", qa_q1_actor_number(owner, "ammo_cells") != 0 ? 1 : -1);
  qa_q1ad_set_number(context, owner, "ammo_cells", 1 - qa_q1_actor_number(owner, "ammo_cells"));
  entity->count = 0;
  qa_actor_id enemy;
  qa_q1am_reference(entity, "enemy", qa_q1am_boss_enemy(context, owner, &enemy) ? &enemy : NULL);
  qa_q1am_field_text(entity, "oldnew.armedThink", "vortex_think_alt");
  qa_q1am_boss_later(context, entity, "vortex_think_alt", 2);
  return entity;
}
void qa_q1am_cleanup_oldnew(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (strcmp(entity->classname, "oldnew_child") == 0 || strcmp(entity->classname, "oldnew_eye") == 0)
      qa_q1am_boss_later(context, entity, "oldnew_cleanup_think", 0.1);
  }
  qa_q1_actor *timer = qa_q1_create(game, "oldnew_cleanup_zombies", NULL, NULL);
  qa_q1ad_set_text(&timer->classname, "");
  qa_q1am_boss_later(context, timer, "oldnew_cleanup_zombies", 2);
}

static void eye_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  qa_q1ad_context *context = ctx;
  if ((FLAGS(entity->movement_flags) & 32) != 0) {
    game->killed_monsters++;
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    qa_q1_emit(game, event);
    entity->movement_flags = FLAGS(entity->movement_flags) & ~3;
    qa_q1am_reference(entity, "enemy", attacker);
    qa_q1_use_targets(game, entity, attacker);
  }
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_COLORED_EXPLOSION;
  event.origin = qa_q1_get_body(game, entity).origin;
  event.color_start = 244;
  event.color_length = 3;
  context->services.emit(context->services.ctx, &event);
  qa_q1_remove(game, entity);
}
/* T_Damage reacts through FoundTarget: a hit armed eye starts shooting 0.1 s later instead of finishing its warm-up. */
static void armed_eye_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double damage) {
  (void)attacker; (void)damage; /* TS (_game, entity) */
  char think[40];
  qa_q1am_copy_name(think, sizeof think, qa_q1_actor_text(entity, "oldnew.armedThink"));
  if (think[0] != '\0' && entity->next_think - game->time > 0.1) qa_q1am_boss_later(ctx, entity, think, 0.1);
}
static void eye_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double take) {
  (void)ctx;
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  qa_q1_body attacker_body;
  const qa_vec3 from = attacker != NULL && qa_q1am_read_body(game, *attacker, &attacker_body) ? attacker_body.origin : QA_Q1_ZERO;
  qa_vec3 delta = qa_sub3(origin, from);
  delta.z = 0;
  const qa_vec3 impulse = qa_scale3(qa_add3(qa_scale3(qa_q1_normalize(delta), 0.8), (qa_vec3){0, 0, 0.2}), take * 10);
  qa_q1am_set_velocity_body(game, entity, qa_add3(qa_q1_get_body(game, entity).velocity, impulse));
}
static void eye_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                      const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (_game, entity, other) */
  if (qa_q1_get_entity(game, &other) == game->world || (entity->has_owner && qa_same_actor(entity->owner, other))) return;
  if (qa_q1_health(game, other) != 0)
    (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 500, NULL, NULL, NULL, NULL);
}
static void eye_chase(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 target = qa_q1am_boss_target(context, entity);
  entity->speed = qa_length3(body.velocity);
  if (entity->speed < 300) entity->speed += 10;
  qa_vec3 velocity = qa_add3(qa_scale3(qa_q1_normalize(qa_sub3(target, body.origin)), 0.3), qa_scale3(qa_q1_normalize(body.velocity), 0.7));
  if (body.origin.z < target.z + 32) {
    velocity.z = 0;
    velocity = qa_q1_normalize(velocity);
  }
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_angles = true;
  patch.angles = qa_q1mp_velocity_angles(velocity);
  patch.has_velocity = true;
  patch.velocity = qa_scale3(velocity, entity->speed);
  qa_q1_set_body(game, entity, &patch);
  qa_q1am_boss_later(context, entity, "eye_chase", 0.1);
  if (fmod(entity->count, 2) == 0) {
    entity->effects = FLAGS(entity->effects) | 2;
    qa_q1_sound_defaults(game, entity->actor.id, "misc/power.wav");
  }
  entity->count++;
}
static void blast(qa_q1ad_context *context, qa_q1_actor *entity, const char *name) {
  qa_q1_services *game = context->game;
  qa_actor_id enemy;
  if (!qa_q1am_boss_enemy(context, entity, &enemy) || !qa_q1_is_player(game, &enemy))
    qa_q1am_reference(entity, "enemy", first_player(game, &enemy));
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 target = qa_q1am_boss_target(context, entity);
  qa_vec3 direction = qa_q1_normalize(qa_sub3(target, body.origin));
  direction = qa_q1_normalize(qa_add3(direction, qa_scale3(qa_cross3_double(direction, (qa_vec3){0, 0, 1}),
                                                           qa_q1ad_qc_cos(entity->count * 15) * 0.5 * qa_q1_actor_number(entity, "aflag"))));
  set_angles(game, entity, qa_q1mp_velocity_angles(direction));
  qa_q1_actor *shot = qa_q1_launch_spike(game, &entity->actor.id, qa_add3(body.origin, qa_scale3(direction, 8)), direction, "spike");
  shot->touch = qa_q1_callback_registry_touch(game->named, shot, PREFIX "blast_touch");
  qa_q1ad_set_text(&shot->model, "progs/rogue/sphere.mdl");
  shot->effects = 64;
  qa_q1_sound(game, entity->actor.id, "weapons/spike2.wav", "weapon", 1, 1);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, 500);
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, shot, &patch);
  shot->angular_velocity = qa_scale3((qa_vec3){300, 300, 300}, qa_q1am_random(game) * 2 - 1);
  qa_q1am_boss_later(context, entity, name, 0.2);
  entity->count++;
  if (entity->count > 72) {
    const qa_actor_id inflictor = qa_q1am_boss_world(context);
    const qa_actor_id attacker = qa_q1am_boss_world(context);
    (void)qa_q1_damage(game, entity->actor.id, inflictor, &attacker, 500, NULL, NULL, NULL, NULL);
  }
}
/* The TS loop registers one closure per name. */
static void blast_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)game; blast(ctx, entity, "blast"); }
static void vortex_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)game; blast(ctx, entity, "vortex_think_alt"); }
static void blast_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (_game, entity, other) */
  if (qa_q1_get_entity(game, &other) == game->world) { qa_q1_remove(game, entity); return; }
  if (strcmp(game->host.classname(game->host.ctx, other), "sphere") == 0 || (entity->has_owner && qa_same_actor(entity->owner, other)))
    return;
  if (strcmp(game->host.classname(game->host.ctx, other), "monster_oldone_new") == 0) { qa_q1_remove(game, entity); return; }
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if (target != NULL && strcmp(target->solid, "trigger") == 0) return;
  if (qa_q1_health(game, other) != 0)
    (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 15, NULL, NULL, NULL, NULL);
  qa_q1_remove(game, entity);
}
static void spammer_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_actor *owner = qa_q1_get_entity(game, qa_q1am_opt(entity->has_owner, &entity->owner));
  const qa_vec3 angles = owner == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, owner).angles;
  set_angles(game, entity, angles);
  static const double offsets[] = {0, 1, -3, 2, -2, 3, -1, 4, 2, -1, -3, 2, -4};
  if (!(entity->count >= 0 && entity->count < 13 && entity->count == floor(entity->count))) { qa_q1_remove(game, entity); return; }
  const double offset = offsets[(size_t)entity->count];
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_add3(angles, (qa_vec3){0, offset * 6, 0}));
  qa_q1_actor *shot = qa_q1_create(game, "spam", NULL, NULL);
  const qa_vec3 origin = qa_add3(qa_add3(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, 50}), qa_scale3(basis.forward, 48));
  shot->has_owner = entity->has_owner;
  shot->owner = entity->owner;
  qa_q1ad_set_text(&shot->model, "progs/rogue/plasma.mdl");
  shot->solid = "bbox";
  shot->movement = "toss";
  shot->effects = 64;
  shot->touch = qa_q1_callback_registry_touch(game->named, shot, PREFIX "spam_touch");
  qa_vec3 a = qa_q1am_boss_target(context, entity);
  qa_vec3 b = qa_q1_get_body(game, entity).origin;
  const double oomph = qa_js_max(200, qa_length3(qa_sub3(a, b)) * 0.8);
  qa_vec3 velocity = qa_scale3(basis.forward, oomph + 25 * entity->count);
  velocity.z = 200;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  patch.has_velocity = true;
  patch.velocity = velocity;
  qa_q1_set_body(game, shot, &patch);
  qa_q1_sound(game, entity->actor.id, "weapons/grenade.wav", "weapon", 1, 1);
  entity->count++;
  qa_q1_link(game, shot);
  qa_q1am_boss_later(context, entity, "spammer_think", 0.1);
}
static void spam_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS (_game, entity, other) */
  qa_q1ad_context *context = ctx;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  if (qa_q1_get_entity(game, &other) == game->world) {
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_velocity = true;
    patch.velocity = QA_Q1_ZERO;
    patch.has_angles = true;
    patch.angles = (qa_vec3){0, qa_q1am_random(game) * 360, 0};
    qa_q1_set_body(game, entity, &patch);
    entity->movement = "none";
    entity->solid = "none";
    qa_q1ad_set_text(&entity->model, "maps/bmodel/b_splash.bsp");
    memset(&entity->touch, 0, sizeof entity->touch);
    qa_q1_link(game, entity);
    qa_q1am_boss_later(context, entity, "spam1", 2);
    return;
  }
  if (qa_q1_health(game, other) != 0) {
    const qa_actor_id inflictor = entity->has_owner ? entity->owner : qa_q1am_boss_world(context);
    const qa_actor_id self = entity->actor.id;
    (void)qa_q1_damage(game, other, inflictor, &self, 10, NULL, NULL, NULL, NULL);
    qa_q1_remove(game, entity);
  }
}
static void spam1(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_LIGHTNING;
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.style = 1;
  event.start = origin;
  event.end = qa_add3(origin, (qa_vec3){0, 0, 500});
  context->services.emit(context->services.ctx, &event);
  qa_q1am_boss_later(context, entity, "spam2", 0.1);
}
static void spam2(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_COLORED_EXPLOSION;
  event.origin = qa_q1_get_body(game, entity).origin;
  event.color_start = 244;
  event.color_length = 3;
  context->services.emit(context->services.ctx, &event);
  const qa_actor_id self = entity->actor.id;
  qa_q1_radius_damage(game, self, &self, 100, game->world == NULL ? NULL : &game->world->actor.id, NULL, NULL);
  qa_q1_remove(game, entity);
}
static void oldnew_swipe(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  const double a = qa_js_pow(entity->count / 50, 2);
  qa_vec3 a2 = qa_q1_get_body(game, entity).angles;
  qa_vec3 b = (qa_vec3){0, qa_q1_actor_number(entity, "aflag") == 0 ? -60 + a * 180 : 60 - a * 180, 0};
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_add3(a2, b));
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin, start = qa_add3(origin, qa_scale3(basis.forward, 130));
  const qa_q1_trace trace = qa_q1am_trace(game, start, qa_add3(origin, qa_scale3(basis.forward, 1000)), QA_Q1_POINT,
                                          &entity->actor.id, true);
  if (trace.has_actor && qa_q1_health(game, trace.actor) != 0) {
    const qa_actor_id inflictor = entity->has_owner ? entity->owner : qa_q1am_boss_world(context);
    (void)qa_q1_damage(game, trace.actor, inflictor, qa_q1am_opt(entity->has_owner, &entity->owner),
                       strcmp(game->host.classname(game->host.ctx, trace.actor), "monster_szombie") == 0 ? 100 : 25,
                       NULL, NULL, NULL, NULL);
  }
  if (fmod(entity->count, 2) == 0) {
    qa_q1ad_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_Q1AD_EVENT_LIGHTNING;
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.style = 3;
    event.start = start;
    event.end = trace.end;
    context->services.emit(context->services.ctx, &event);
  }
  if (qa_q1_actor_number(entity, "t_width") < game->time) {
    qa_q1_sound(game, entity->actor.id, "weapons/lhit.wav", "weapon", 1, 1);
    qa_q1ad_set_number(context, entity, "t_width", game->time + 0.6);
  }
  entity->count++;
  if (entity->count > 50) qa_q1_schedule(game, entity, 0.025, qa_q1_callback_registry_action(game->named, entity, "SUB_Remove"));
  else qa_q1am_boss_later(context, entity, "oldnew_swipe", 0.025);
}
static void cleanup_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const qa_actor_id self = entity->actor.id;
  (void)qa_q1_damage(game, self, self, &self, 5000, NULL, NULL, NULL, NULL);
}
static void cleanup_zombies(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_actor *zombie = NULL;
  for (size_t i = 0; i < game->entity_count && zombie == NULL; i++)
    if (strcmp(game->entities[i]->classname, "monster_szombie") == 0) zombie = game->entities[i];
  if (zombie != NULL) {
    const qa_actor_id inflictor = qa_q1am_boss_world(context);
    const qa_actor_id attacker = qa_q1am_boss_world(context);
    (void)qa_q1_damage(game, zombie->actor.id, inflictor, &attacker, 500, NULL, NULL, NULL, NULL);
    qa_q1am_boss_later(context, entity, "oldnew_cleanup_zombies", 0.2 + qa_q1am_random(game) * 0.5);
    return;
  }
  qa_q1am_boss_later(context, entity, "oldnew_cleanup_zombies", 1.5);
}
void qa_q1am_register_oldnew_children(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_handlers handlers;
#define REGISTER(NAME, FIELD, FN)                                    \
  do {                                                               \
    memset(&handlers, 0, sizeof handlers);                           \
    handlers.ctx = context;                                          \
    handlers.FIELD = FN;                                             \
    qa_q1_callback_registry_register(game->named, PREFIX NAME, handlers); \
  } while (0)
  REGISTER("eye_die", die, eye_die);
  REGISTER("armed_eye_pain", pain, armed_eye_pain);
  REGISTER("eye_pain", pain, eye_pain);
  REGISTER("eye_touch", touch, eye_touch);
  REGISTER("eye_chase", action, eye_chase);
  REGISTER("blast", action, blast_action);
  REGISTER("vortex_think_alt", action, vortex_action);
  REGISTER("blast_touch", touch, blast_touch);
  REGISTER("spammer_think", action, spammer_think);
  REGISTER("spam_touch", touch, spam_touch);
  REGISTER("spam1", action, spam1);
  REGISTER("spam2", action, spam2);
  REGISTER("oldnew_swipe", action, oldnew_swipe);
  REGISTER("oldnew_cleanup_think", action, cleanup_think);
  REGISTER("oldnew_cleanup_zombies", action, cleanup_zombies);
#undef REGISTER
}
