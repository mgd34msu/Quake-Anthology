/* Port of src/content/q1/addons/monsters/bosses/ghost.ts.
 * quakec_mg3/monsters/mg3_player_ghost.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/ghost.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/frames/ghost.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/base/map_entities.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/missionpacks/types.h"
#include "../support.h"

#define PREFIX "mg3:ghost"

static const char *const ghost_classnames[] = {"monster_ghost"};
static const qa_q1_base_monster_species ghost_spec = {
  .species = "army", .classnames = ghost_classnames, .classname_count = 1, .model = "player", .head = NULL,
  .health = 10, .gib_health = -INFINITY, .gibs = NULL, .gib_count = 0, .bounds = {{-16, -16, -24}, {16, 16, 32}},
  .stand = "ghost_stand1", .walk = "ghost_run1", .run = "ghost_run1", .sight = "", .missile = NULL, .melee = false,
  .movement = "fly",
};

static void ghost_spawn(qa_q1_base_monster *monster);
static void ghost_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void ghost_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops ghost_ops = {.spawn = ghost_spawn, .die = ghost_die, .destroy = ghost_destroy};
bool qa_q1am_is_ghost(const qa_q1_base_monster *monster) { return monster->ops == &ghost_ops; }
static qa_q1am_ghost *ghost_of(qa_q1_base_monster *monster) { return (qa_q1am_ghost *)monster; }

static void ghost_frame_action(void *ctx, qa_q1_base_monster *monster) {
  if (!qa_q1am_is_ghost(monster)) qa_range_error("Ghost callback received another source controller");
  qa_q1am_ghost_action(ghost_of(monster), ctx);
}
static qa_vec3 wander_goal(qa_q1_services *game, const qa_q1_actor *entity) {
  const qa_vec3 angles = {0, qa_q1am_random(game) * 360, 0};
  return qa_add3(entity->dest1, qa_scale3(qa_q1_make_vectors(game, angles).forward, entity->wait));
}
static void ghost_spawn(qa_q1_base_monster *monster) {
  qa_q1am_ghost *ghost = ghost_of(monster);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = ghost->context;
  if (qa_q1ad_removed_for_runes(context, entity) || qa_q1ad_removed_outside_coop(context, entity, true)) return;
  const double choice = qa_q1am_random(game) * 5;
  qa_q1am_field_text(entity, "ghost.death", choice < 1 ? "ghost_diea1" : choice < 2 ? "ghost_dieb1"
                                            : choice < 3 ? "ghost_diec1" : choice < 4 ? "ghost_died1" : "ghost_diee1");
  if (entity->wait == 0 || isnan(entity->wait)) entity->wait = 128; /* TS ||= */
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 10);
  entity->max_health = 10;
  entity->solid = "slidebox";
  qa_q1ad_set_text(&entity->model, "progs/player.mdl");
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = false;
  entity->path_end = qa_q1_callback_registry_action(game->named, entity, PREFIX ":monster_stand");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, PREFIX ":touch");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  entity->movement = "fly";
  qa_q1_set_bounds(game, entity, ghost_spec.bounds);
  entity->dest1 = qa_q1_base_monster_origin(monster);
  entity->dest2 = wander_goal(game, entity);
  qa_q1_base_monster_play(monster, ghost_spec.stand);
  entity->count = floor(qa_q1am_random(game) * 5 + 0.5) + 3;
}
static void ghost_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  (void)attacker; /* TS die(_attacker) */
  qa_q1_actor_set_damageable(monster->entity, false);
  memset(&monster->entity->touch, 0, sizeof monster->entity->touch);
  char death[64];
  qa_q1am_copy_name(death, sizeof death, qa_q1_actor_text(monster->entity, "ghost.death"));
  qa_q1_base_monster_play(monster, death);
}
void qa_q1am_ghost_action(qa_q1am_ghost *ghost, const char *name) {
  qa_q1_base_monster *monster = &ghost->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (strcmp(name, "ghost:ghost_stand1") == 0) {
    qa_q1am_set_velocity_body(game, entity, QA_Q1_ZERO);
  } else if (strcmp(name, "ghost:ghost_stand5") == 0) {
    entity->count--;
    if (entity->count == 0) {
      qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "ghost_run1");
      entity->dest2 = wander_goal(game, entity);
    }
  } else if (strcmp(name, "ghost:ghost_run1") == 0) {
    qa_vec3 delta = qa_sub3(entity->dest2, qa_q1_base_monster_origin(monster));
    delta.z = 0;
    const qa_vec3 direction = qa_q1_normalize(delta);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_angles = true;
    patch.angles = qa_q1mp_velocity_angles(direction);
    patch.has_velocity = true;
    patch.velocity = qa_scale3(direction, 150);
    qa_q1_set_body(game, entity, &patch);
  } else if (strcmp(name, "ghost:ghost_run6") == 0) {
    entity->count = floor(qa_q1am_random(game) * 5 + 0.5) + 3;
  } else if (strcmp(name, "ghost:ghost_diea1") == 0) {
    if (entity->water_level == 3) {
      qa_q1_actor *timer = qa_q1_create(game, "death_bubbles", NULL, NULL);
      timer->has_owner = true;
      timer->owner = entity->actor.id;
      timer->count = 20;
      qa_q1_set_origin(game, timer, qa_q1_base_monster_origin(monster));
      qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, PREFIX ":death_bubbles"));
      qa_q1_sound(game, entity->actor.id, "player/h2odeath.wav", "voice", 0, 1);
    } else {
      char path[40];
      (void)snprintf(path, sizeof path, "player/death%.0f.wav", floor(qa_q1am_random(game) * 4 + 1.5));
      qa_q1am_field_text(entity, "noise", path);
      qa_q1_sound(game, entity->actor.id, path, "voice", 0, 1);
    }
    qa_q1am_set_velocity_body(game, entity, QA_Q1_ZERO);
  } else if (strcmp(name, "ghost:ghost_diea11") == 0) {
    (void)qa_q1_spawn_teleport_fog(game, qa_q1_base_monster_origin(monster));
    qa_q1_remove(game, entity);
  } else {
    qa_range_error("Unknown ghost action %s", name);
  }
}
void qa_q1am_ghost_touch(qa_q1am_ghost *ghost, qa_actor_id other) {
  qa_q1_base_monster *monster = &ghost->base;
  qa_q1_services *game = monster->game;
  (void)qa_q1am_random(game); /* TS discards one host.random() draw */
  if (qa_q1_health(game, other) == 0 || monster->state->pain_finished > game->time || !qa_q1_is_player(game, &other))
    return;
  memset(&monster->entity->touch, 0, sizeof monster->entity->touch);
  qa_q1_base_monster_die(monster, &other);
}
bool qa_q1am_ghost_init(qa_q1am_ghost *ghost, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(ghost, 0, sizeof *ghost);
  ghost->context = context;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_ghost_frames;
  source.actions = qa_q1am_frame_actions(&qa_q1am_ghost_frames, ghost_frame_action, &source.action_count);
  if (!qa_q1_base_monster_init(&ghost->base, context->game, entity, &ghost_spec,
                               qa_q1_base_as_monster_services(context->base), &source))
    return false;
  ghost->base.ops = &ghost_ops;
  return true;
}
static qa_q1_base_monster *create_ghost(void *ctx, qa_q1_actor *entity) {
  qa_q1am_ghost *ghost = malloc(sizeof *ghost);
  if (ghost == NULL) return NULL;
  if (!qa_q1am_ghost_init(ghost, ctx, entity)) { free(ghost); return NULL; }
  return &ghost->base;
}
static void touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                  const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS (_game, entity, other) */
  qa_q1am_ghost_touch(ghost_of(qa_q1am_require_boss(ctx, entity)), other);
}
static void death_bubbles(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  (void)ctx; /* TS (_game, timer) over the captured game */
  qa_q1_actor *owner = qa_q1_get_entity(game, qa_q1am_opt(timer->has_owner, &timer->owner));
  if (owner == NULL || owner->water_level != 3) return;
  (void)qa_q1_spawn_bubble(game, qa_add3(qa_q1_get_body(game, owner).origin, (qa_vec3){0, 0, 24}), NULL, false);
  timer->count--;
  if (timer->count <= 0) qa_q1_remove(game, timer);
  else qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, PREFIX ":death_bubbles"));
}
void qa_q1am_register_ghost(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1am_boss_registry *ghosts = qa_q1am_register_boss_controllers(context, PREFIX, "monster_ghost", create_ghost, context);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = ghosts;
  handlers.touch = touch;
  qa_q1_callback_registry_register(game->named, PREFIX ":touch", handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.action = death_bubbles;
  qa_q1_callback_registry_register(game->named, PREFIX ":death_bubbles", handlers);
}
