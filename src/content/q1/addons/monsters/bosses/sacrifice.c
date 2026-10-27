/* Port of src/content/q1/addons/monsters/bosses/sacrifice.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/sacrifice.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FR(x) ((double)(float)(x))
#define FLAGS(x) ((int32_t)(x))

static void gib(qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_add3(game->body(game->ctx, entity).origin, (qa_vec3){0, 0, -32});
  game->set_body(game->ctx, entity, &patch);
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (int i = 0; i < 3; i++)
    game->throw_gib(game->ctx, game, game->body(game->ctx, entity).origin, models[i], -10);
  game->sound(game->ctx, entity, game->random(game->ctx) < 0.5 ? "player/gib.wav" : "player/udeath.wav",
              "voice", 0);
  game->use_targets(game->ctx, entity, entity->has_activator, entity->activator);
  game->remove(game->ctx, entity);
}
static void gib_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_other,
                    qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)ctx; (void)has_other; (void)other;
  entity->has_activator = has_activator;
  entity->activator = activator;
  gib(game, entity);
}
static void gib_die(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has, qa_actor_id attacker) {
  (void)ctx; (void)has; (void)attacker;
  gib(game, entity);
}
static void animate(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx;
  entity->count++;
  if (entity->count > 75) entity->count = 5;
  entity->frame = entity->count;
  game->schedule(game->ctx, entity, 0.1, game->named_action(game->ctx, entity, "mg3:sacrifice_animate"));
}
static void think(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx;
  entity->count = FR(entity->count + 0.1);
  game->set_origin(game->ctx, entity,
                   qa_add3(qa_q1am_actor_vector(entity, "dest"), (qa_vec3){0, 0, cos(entity->count * 90) * 16}));
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, 3.6, 0});
  game->set_body(game->ctx, entity, &patch);
  game->schedule(game->ctx, entity, 0.1, game->named_action(game->ctx, entity, "mg3:sacrifice_think"));
}
static void sacrifice_spawn(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  entity->use = game->named_use(game->ctx, entity, "mg3:sacrifice_gib");
  entity->die = game->named_die(game->ctx, entity, "mg3:sacrifice_gib");
  game->combat_set_health(game->ctx, entity->actor, 100);
  entity->max_health = 100;
  entity->solid = "slidebox";
  bool damageable = (FLAGS(entity->spawnflags) & 1) == 0;
  qa_q1am_actor_set_damageable(game, entity, damageable);
  entity->aimed_damage = damageable;
  entity->movement = "step";
  if ((FLAGS(entity->spawnflags) & 2) != 0) {
    (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/player_hanging.mdl");
    entity->count = 0;
    entity->angular_velocity = (qa_vec3){0, 36, 0};
    context->set_vector(context->ctx, entity, "dest", game->body(game->ctx, entity).origin);
    game->schedule(game->ctx, entity, 0.1, game->named_action(game->ctx, entity, "mg3:sacrifice_think"));
  } else {
    entity->count = 5 + floor(game->random(game->ctx) * 65 + 0.5);
    (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/player_hanging_animated.mdl");
    entity->frame = entity->count;
    game->schedule(game->ctx, entity, 0.1, game->named_action(game->ctx, entity, "mg3:sacrifice_animate"));
  }
  game->set_bounds(game->ctx, entity, (qa_bounds){{-16, -16, -56}, {16, 16, 0}});
}
void qa_q1am_register_sacrifice(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_handlers handlers = {0};
  handlers.use = gib_use;
  handlers.die = gib_die;
  game->named_register(game->ctx, "mg3:sacrifice_gib", handlers);
  handlers.use = NULL;
  handlers.die = NULL;
  handlers.action = animate;
  game->named_register(game->ctx, "mg3:sacrifice_animate", handlers);
  handlers.action = think;
  game->named_register(game->ctx, "mg3:sacrifice_think", handlers);
  game->register_spawn(game->ctx, "misc_sacrifice", sacrifice_spawn, context);
}
