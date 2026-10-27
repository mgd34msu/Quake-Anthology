/* Port of src/content/q1/addons/monsters/bosses/ghost.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/ghost.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/frames/ghost.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/foundation/support.h"

static const char *const ghost_classnames[] = {"monster_ghost"};
static const qa_q1am_species ghost_spec = {
  "army", NULL, "player", NULL, "ghost_stand1", "ghost_run1", "ghost_run1",
  "", NULL, "fly", ghost_classnames, 1, NULL, 0,
  10, -INFINITY, {{-16, -16, -24}, {16, 16, 32}}, false,
};
static void ghost_dispatch(qa_q1am_monster *monster, const char *name) {
  qa_q1am_ghost_action(monster->owner, name);
}
static const qa_q1am_source *ghost_source(void) {
  static const qa_q1am_source *source;
  if (source == NULL)
    source = qa_q1am_boss_build_source("mg3:ghost", &qa_q1am_ghost_frames, QA_Q1AM_KIND_GHOST,
                                       "Ghost callback received another source controller",
                                       ghost_dispatch, NULL, 0);
  return source;
}
static qa_q1am_ghost *as_ghost(qa_q1am_monster *m) { return m->owner; }
static void ghost_spawn(qa_q1am_monster *m) {
  qa_q1am_ghost *self = as_ghost(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (context->removed_for_runes(context->ctx, entity) ||
      context->removed_outside_coop(context->ctx, entity))
    return;
  double choice = game->random(game->ctx) * 5;
  qa_q1am_actor_set_field(entity, "ghost.death",
                          choice < 1 ? "ghost_diea1" : choice < 2 ? "ghost_dieb1"
                          : choice < 3 ? "ghost_diec1" : choice < 4 ? "ghost_died1" : "ghost_diee1");
  if (entity->wait == 0) entity->wait = 128;
  game->combat_set_health(game->ctx, entity->actor, 10);
  entity->max_health = 10;
  entity->solid = "slidebox";
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/player.mdl");
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->aimed_damage = false;
  entity->path_end = game->named_action(game->ctx, entity, "mg3:ghost:monster_stand");
  entity->touch = game->named_touch(game->ctx, entity, "mg3:ghost:touch");
  entity->die = game->named_die(game->ctx, entity, "mg3:ghost:monster_die");
  entity->movement = "fly";
  game->set_bounds(game->ctx, entity, ghost_spec.bounds);
  entity->dest1 = qa_q1am_monster_origin(m);
  entity->dest2 = qa_add3(entity->dest1,
                          qa_scale3(qa_q1am_game_make_vectors(game, (qa_vec3){0, game->random(game->ctx) * 360, 0}).forward,
                                    entity->wait));
  m->ops.play(m, ghost_spec.stand);
  entity->count = floor(game->random(game->ctx) * 5 + 0.5) + 3;
}
static void ghost_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  (void)has; (void)attacker;
  m->ops.play(m, qa_q1am_actor_text(m->entity, "ghost.death"));
}
void qa_q1am_ghost_action(qa_q1am_ghost *ghost, const char *name) {
  qa_q1am_monster *m = &ghost->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_body_patch patch = {0};
  if (strcmp(name, "ghost:ghost_stand1") == 0) {
    patch.has_velocity = true;
    patch.velocity = QA_Q1AM_ZERO;
    game->set_body(game->ctx, entity, &patch);
  } else if (strcmp(name, "ghost:ghost_stand5") == 0) {
    entity->count--;
    if (entity->count == 0) {
      qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "ghost_run1");
      entity->dest2 = qa_add3(entity->dest1,
                              qa_scale3(qa_q1am_game_make_vectors(game, (qa_vec3){0, game->random(game->ctx) * 360, 0}).forward,
                                        entity->wait));
    }
  } else if (strcmp(name, "ghost:ghost_run1") == 0) {
    qa_vec3 delta = qa_sub3(entity->dest2, qa_q1am_monster_origin(m));
    delta.z = 0;
    qa_vec3 direction = qa_q1am_normalize(delta);
    patch.has_angles = true;
    patch.angles = qa_q1am_velocity_angles(direction);
    patch.has_velocity = true;
    patch.velocity = qa_scale3(direction, 150);
    game->set_body(game->ctx, entity, &patch);
  } else if (strcmp(name, "ghost:ghost_run6") == 0) {
    entity->count = floor(game->random(game->ctx) * 5 + 0.5) + 3;
  } else if (strcmp(name, "ghost:ghost_diea1") == 0) {
    if (entity->water_level == 3) {
      qa_q1am_actor *timer = game->create(game->ctx, "death_bubbles");
      timer->has_owner = true;
      timer->owner = entity->actor.id;
      timer->count = 20;
      game->set_origin(game->ctx, timer, qa_q1am_monster_origin(m));
      game->schedule(game->ctx, timer, 0.1, game->named_action(game->ctx, timer, "mg3:ghost:death_bubbles"));
      game->sound(game->ctx, entity, "player/h2odeath.wav", "voice", 0);
    } else {
      char path[64];
      (void)snprintf(path, sizeof path, "player/death%d.wav", (int)floor(game->random(game->ctx) * 4 + 1.5));
      qa_q1am_actor_set_field(entity, "noise", path);
      game->sound(game->ctx, entity, path, "voice", 0);
    }
    patch.has_velocity = true;
    patch.velocity = QA_Q1AM_ZERO;
    game->set_body(game->ctx, entity, &patch);
  } else if (strcmp(name, "ghost:ghost_diea11") == 0) {
    game->spawn_teleport_fog(game->ctx, game, qa_q1am_monster_origin(m));
    game->remove(game->ctx, entity);
  } else {
    qa_range_error("Unknown ghost action %s", name);
  }
}
void qa_q1am_ghost_touch(qa_q1am_ghost *ghost, qa_actor_id other) {
  qa_q1am_monster *m = &ghost->base;
  m->game->random(m->game->ctx);
  if (m->game->health(m->game->ctx, other) == 0 || m->state->pain_finished > m->game->time ||
      !m->game->is_player(m->game->ctx, true, other))
    return;
  m->entity->touch.present = false;
  m->ops.die(m, true, other);
}
void qa_q1am_ghost_init(qa_q1am_ghost *ghost, qa_q1am_context *context, qa_q1am_actor *entity) {
  memset(ghost, 0, sizeof *ghost);
  ghost->context = context;
  qa_q1am_monster_init(&ghost->base, context->game, entity, &ghost_spec, context->base, ghost_source());
  ghost->base.owner = ghost;
  ghost->base.kind = QA_Q1AM_KIND_GHOST;
  ghost->super_ops = ghost->base.ops;
  ghost->base.ops.spawn = ghost_spawn;
  ghost->base.ops.die = ghost_die;
}
static qa_q1am_monster *create_ghost(void *ctx, qa_q1am_actor *entity) {
  qa_q1am_ghost *ghost = malloc(sizeof *ghost);
  if (!ghost) qa_range_error("Ghost allocation failed");
  qa_q1am_ghost_init(ghost, ctx, entity);
  return &ghost->base;
}
static void destroy_ghost(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  free(monster->owner);
}
static void touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                          bool has_normal, qa_vec3 normal) {
  (void)game; (void)has_normal; (void)normal;
  qa_q1am_ghost_touch(qa_q1am_require_boss(ctx, entity)->owner, other);
}
static void death_bubbles(void *ctx, qa_q1am_game *game, qa_q1am_actor *timer) {
  (void)ctx;
  qa_q1am_actor *owner = game->entity(game->ctx, timer->has_owner, timer->owner);
  if (owner == NULL || owner->water_level != 3) return;
  game->spawn_bubble(game->ctx, game, qa_add3(game->body(game->ctx, owner).origin, (qa_vec3){0, 0, 24}));
  timer->count--;
  if (timer->count <= 0) game->remove(game->ctx, timer);
  else game->schedule(game->ctx, timer, 0.1, game->named_action(game->ctx, timer, "mg3:ghost:death_bubbles"));
}
void qa_q1am_register_ghost(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_boss_create create = {context, create_ghost, destroy_ghost};
  qa_q1am_boss_registry *ghosts =
      qa_q1am_register_boss_controllers(context, "mg3:ghost", "monster_ghost", create);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = ghosts;
  handlers.touch = touch_handler;
  game->named_register(game->ctx, "mg3:ghost:touch", handlers);
  handlers.touch = NULL;
  handlers.action = death_bubbles;
  game->named_register(game->ctx, "mg3:ghost:death_bubbles", handlers);
}
