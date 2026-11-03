/* Port of src/content/q1/addons/monsters/bosses/sacrifice.ts.
 * quakec_mg3/monsters/mg3_sacrifice.qc. Copyright (C) 1996-2026 id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/sacrifice.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/addons/qc_math.h"
#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

static void set_activator(qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
}
static void gib(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1am_set_origin_body(game, entity, qa_add3(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, -32}));
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (size_t i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, qa_q1_get_body(game, entity).origin, models[i], -10);
  qa_q1_sound(game, entity->actor.id, qa_q1am_random(game) < 0.5 ? "player/gib.wav" : "player/udeath.wav", "voice", 0, 1);
  const qa_actor_id activator = entity->activator;
  qa_q1_use_targets(game, entity, entity->has_activator ? &activator : NULL);
  qa_q1_remove(game, entity);
}
static void gib_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                    const qa_actor_id *activator) {
  (void)ctx; (void)other; /* TS (_game, entity, _other, activator) */
  set_activator(entity, activator);
  gib(game, entity);
}
static void gib_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  set_activator(entity, attacker);
  qa_q1_use_targets(game, entity, attacker);
  gib(game, entity);
}
static void animate(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->count++;
  if (entity->count > 75) entity->count = 5;
  entity->frame = entity->count;
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "mg3:sacrifice_animate"));
}
static void think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->count = QA_Q1AM_FR(entity->count + 0.1);
  qa_q1_set_origin(game, entity,
                   qa_add3(qa_q1_actor_vector(entity, "dest"), (qa_vec3){0, 0, qa_q1ad_qc_cos(entity->count * 90) * 16}));
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_angles = true;
  patch.angles = qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, 3.6, 0});
  qa_q1_set_body(game, entity, &patch);
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "mg3:sacrifice_think"));
}
static void sacrifice_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "mg3:sacrifice_gib");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "mg3:sacrifice_gib");
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 100);
  entity->max_health = 100;
  entity->solid = "slidebox";
  qa_q1_actor_set_damageable(entity, (QA_Q1AM_FLAGS(entity->spawnflags) & 1) == 0);
  entity->aimed_damage = qa_q1_actor_damageable(entity);
  entity->movement = "step";
  if ((QA_Q1AM_FLAGS(entity->spawnflags) & 2) != 0) {
    qa_q1ad_set_text(&entity->model, "progs/player_hanging.mdl");
    entity->count = 0;
    entity->angular_velocity = (qa_vec3){0, 36, 0};
    qa_q1ad_set_vector(context, entity, "dest", qa_q1_get_body(game, entity).origin);
    qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "mg3:sacrifice_think"));
  } else {
    entity->count = 5 + floor(qa_q1am_random(game) * 65 + 0.5);
    qa_q1ad_set_text(&entity->model, "progs/player_hanging_animated.mdl");
    entity->frame = entity->count;
    qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "mg3:sacrifice_animate"));
  }
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -56}, {16, 16, 0}});
}
void qa_q1am_register_sacrifice(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.use = gib_use;
  handlers.die = gib_die;
  qa_q1_callback_registry_register(game->named, "mg3:sacrifice_gib", handlers);
  handlers.use = NULL;
  handlers.die = NULL;
  handlers.action = animate;
  qa_q1_callback_registry_register(game->named, "mg3:sacrifice_animate", handlers);
  handlers.action = think;
  qa_q1_callback_registry_register(game->named, "mg3:sacrifice_think", handlers);
  qa_q1_register_spawn(game, "misc_sacrifice", sacrifice_spawn, context);
}
