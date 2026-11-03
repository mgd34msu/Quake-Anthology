/* Port of src/content/q1/addons/monsters/ordinary/slime.ts.
 * quakec_mg3/monsters/tarbaby.qc: monster_slime, SlimeMissile, SlimeMissileTouch and SlimeStartCheck.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/slime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support.h"

static const qa_bounds small_bounds = {{-16, -16, -24}, {16, 16, 32}};

/* The TS registerSlime closure environment. */
typedef struct slime_registration {
  qa_q1ad_context *context;
  qa_q1am_monster_store monsters;
  qa_q1am_monster_factory create;
  struct slime_registration *next;
} slime_registration;
static slime_registration *all_registrations;

static void named(char *out, size_t size, const qa_q1ad_context *context, const char *suffix) {
  (void)snprintf(out, size, "%s:ordinary:%s", qa_q1ad_program_name(context->program), suffix);
}
/* tbaby_explode without the final BecomeExplosion: the sprite is a visual that ends in a removal. */
static void explode(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_sound_defaults(game, entity->actor.id, "blob/death1.wav");
  qa_q1_effect(game, "tar-explosion", qa_sub3(body.origin, qa_scale3(qa_q1_normalize(body.velocity), 8)), NULL, 1);
  qa_q1_remove(game, entity);
}
static void slime_missile(qa_q1ad_context *context, qa_q1_base_monster *monster, qa_vec3 origin, double offset) {
  qa_q1_services *game = context->game;
  qa_q1_actor *entity = monster->entity;
  const qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  qa_q1_actor *missile = qa_q1_create(game, "slime_missile", NULL, NULL);
  qa_q1ad_set_text(&missile->classname, "");
  missile->has_owner = true;
  missile->owner = entity->actor.id;
  qa_q1am_reference(missile, "enemy", qa_q1_base_monster_enemy(monster));
  qa_q1ad_set_number(context, missile, "slime", qa_q1_actor_number(entity, "slime"));
  missile->solid = "bbox";
  missile->movement = "bounce";
  qa_q1ad_set_text(&missile->model, "progs/v_spike.mdl");
  const qa_vec3 forward = qa_q1_make_vectors(game, (qa_vec3){angles.x, angles.y + offset, angles.z}).forward;
  qa_vec3 velocity = qa_scale3(forward, 200);
  velocity.z = 200;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_add3(origin, (qa_vec3){0, 0, 16});
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, missile, &patch);
  missile->angular_velocity = (qa_vec3){300, 300, 300};
  char name[64];
  named(name, sizeof name, context, "slime_missile_touch");
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, name);
  qa_q1_schedule(game, missile, 5, qa_q1_callback_registry_action(game->named, missile, "SUB_Remove"));
  qa_q1_link(game, missile);
}
static double crandom(qa_q1_services *game) { return 2 * (qa_q1am_random(game) - 0.5); }
void qa_q1am_tarbaby_die2(void *ctx, qa_q1_base_monster *monster) {
  qa_q1ad_context *context = ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_actor_id id = entity->actor.id;
  qa_q1_radius_damage(game, id, &id, 120, NULL, NULL, NULL);
  qa_q1_sound_defaults(game, entity->actor.id, "blob/death1.wav");
  const qa_vec3 origin = qa_sub3(body.origin, qa_scale3(qa_q1_normalize(body.velocity), 8));
  qa_q1_effect(game, "tar-explosion", origin, NULL, 1);
  if (qa_q1_actor_number(entity, "slime") != 0) {
    qa_q1ad_set_number(context, entity, "slime", qa_q1_actor_number(entity, "slime") - 1);
    double offset = 180 + crandom(game) * 20;
    if (qa_q1am_random(game) > 0.7) {
      offset = 120 + crandom(game) * 20;
      slime_missile(context, monster, origin, offset * -1);
    }
    slime_missile(context, monster, origin, 0 + crandom(game) * 20);
    slime_missile(context, monster, origin, offset);
  }
  qa_q1_remove(game, entity);
}
static void slime_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  slime_registration *state = ctx;
  qa_q1ad_context *context = state->context;
  (void)qa_q1_services_precache_model(game, "progs/v_spike.mdl");
  (void)qa_q1_services_precache_sound(game, "shalrath/attack2.wav");
  qa_q1ad_set_number(context, entity, "slime", 2);
  qa_q1ad_set_text(&entity->classname, "monster_tarbaby");
  qa_q1ad_set_number(context, entity, "aflag", 1);
  qa_q1am_field_text(entity, "addon.monsterClass", "monster_tarbaby");
  qa_q1_base_monster *value = state->create(context, entity);
  state->monsters.set(state->monsters.ctx, entity->actor, value);
  qa_q1_base_monster_spawn(value);
}
static void slime_missile_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS (_game, missile, other) */
  slime_registration *state = ctx;
  qa_q1ad_context *context = state->context;
  if (missile->has_owner && qa_same_actor(missile->owner, other)) return;
  const qa_vec3 origin = qa_q1_get_body(game, missile).origin;
  if (qa_q1_health(game, other) != 0) {
    const qa_actor_id id = missile->actor.id;
    qa_q1_radius_damage(game, id, &id, 120, NULL, NULL, NULL);
    explode(context, missile);
    return;
  }
  qa_q1_actor *tar = qa_q1_create(game, "monster_tarbaby", NULL, NULL);
  qa_q1am_field_text(tar, "addon.monsterClass", "monster_tarbaby");
  qa_q1_base_monster *value = state->create(context, tar);
  state->monsters.set(state->monsters.ctx, tar->actor, value);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_add3(origin, (qa_vec3){0, 0, 25});
  patch.has_angles = true;
  patch.angles = (qa_vec3){0, 2 * (qa_q1am_random(game) - 0.5) * 180, 0};
  qa_q1_set_body(game, tar, &patch);
  qa_q1ad_set_number(context, tar, "slime", qa_q1_actor_number(missile, "slime"));
  qa_actor_id enemy;
  qa_q1_base_monster_set_enemy(value, qa_q1_actor_reference_get(missile, "enemy", &enemy) ? &enemy : NULL);
  tar->ideal_yaw = qa_q1_get_body(game, tar).angles.y;
  if (tar->yaw_speed == 0 || tar->yaw_speed != tar->yaw_speed) tar->yaw_speed = 20; /* TS ||= */
  qa_q1ad_set_vector(context, tar, "view_ofs", (qa_vec3){0, 0, 25});
  qa_gameplay_authority_set_health(game->host.combat, tar->actor, 80);
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = "q1:monsters";
  qa_gameplay_authority_set_traits(game->host.combat, tar->actor, traits);
  tar->movement_flags = QA_Q1AM_FLAGS(tar->movement_flags) | 32;
  char name[64];
  named(name, sizeof name, context, "monster_pain");
  tar->pain = qa_q1_callback_registry_pain(game->named, tar, name);
  named(name, sizeof name, context, "monster_die");
  tar->die = qa_q1_callback_registry_die(game->named, tar, name);
  named(name, sizeof name, context, "monster_stand");
  tar->path_end = qa_q1_callback_registry_action(game->named, tar, name);
  qa_q1ad_set_number(context, tar, "allowPathFind", 1);
  qa_q1ad_set_number(context, tar, "combat_style", 2);
  qa_q1ad_set_number(context, tar, "lefty", 1);
  qa_q1ad_set_number(context, tar, "state", 1);
  value->lefty = true;
  tar->solid = "slidebox";
  tar->movement = "step";
  qa_q1ad_set_text(&tar->model, "progs/tarbaby.mdl");
  qa_q1am_field_text(tar, "mdl", "progs/tarbaby.mdl");
  qa_q1_set_bounds(game, tar, small_bounds);
  value->state->pause_until = 99999999;
  named(name, sizeof name, context, "slime_start_check");
  qa_q1_schedule(game, tar, 0.1, qa_q1_callback_registry_action(game->named, tar, name));
  qa_q1_remove(game, missile);
}
static void slime_start_check(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  slime_registration *state = ctx;
  if (!qa_q1am_walk_move(game, entity->actor, 0, 0)) {
    explode(state->context, entity);
    return;
  }
  entity->aimed_damage = true;
  qa_q1_actor_set_damageable(entity, true);
  game->total_monsters++;
  char name[64];
  named(name, sizeof name, state->context, "monster_found");
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, name));
}
void qa_q1am_register_slime(qa_q1ad_context *context, qa_q1am_monster_store monsters, qa_q1am_monster_factory create) {
  qa_q1_services *game = context->game;
  slime_registration *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("out of memory");
  state->context = context;
  state->monsters = monsters;
  state->create = create;
  state->next = all_registrations;
  all_registrations = state;
  qa_q1_register_spawn(game, "monster_slime", slime_spawn, state);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = state;
  handlers.touch = slime_missile_touch;
  char name[64];
  named(name, sizeof name, context, "slime_missile_touch");
  qa_q1_callback_registry_register(game->named, name, handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = state;
  handlers.action = slime_start_check;
  named(name, sizeof name, context, "slime_start_check");
  qa_q1_callback_registry_register(game->named, name, handlers);
}
void qa_q1am_release_slime(qa_q1_services *game) {
  for (slime_registration **link = &all_registrations; *link != NULL;) {
    slime_registration *state = *link;
    if (state->context->game != game) { link = &state->next; continue; }
    *link = state->next;
    free(state);
  }
}
