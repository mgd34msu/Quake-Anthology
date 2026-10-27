/* Port of src/content/q1/addons/monsters/startup.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/startup.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

static const qa_bounds small_bounds = {{-16, -16, -24}, {16, 16, 40}};
static const qa_bounds large_bounds = {{-32, -32, -24}, {32, 32, 64}};

/* TS WeakMap<Q1EntityServices, Set<string>> mirror. */
typedef struct start_names {
  qa_q1am_game *game;
  char (*names)[200];
  size_t count, cap;
  struct start_names *next;
} start_names;
static start_names *all_start_names;
static start_names *names_for(qa_q1am_game *game, bool create) {
  for (start_names *entry = all_start_names; entry; entry = entry->next)
    if (entry->game == game) return entry;
  if (!create) return NULL;
  start_names *entry = calloc(1, sizeof *entry);
  if (!entry) qa_range_error("MG3 startup allocation failed");
  entry->game = game;
  entry->next = all_start_names;
  all_start_names = entry;
  return entry;
}

static const char *prefix(const qa_q1am_monster *monster) {
  return monster->source ? monster->source->callback_prefix : "base";
}

void qa_q1am_init_mg3_monster(qa_q1am_monster *monster, qa_q1am_context *context,
                              const char *model, double type, double size) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  if (game->options.deathmatch != 0 || context->removed_for_runes(context->ctx, entity) ||
      context->removed_outside_coop(context->ctx, entity)) {
    if (game->live(game->ctx, entity)) game->remove(game->ctx, entity);
    return;
  }
  entity->movement_flags = (double)(FLAGS(entity->movement_flags) | 16384);
  qa_q1am_actor_set_field(entity, "mdl", model);
  context->set_number(context->ctx, entity, "lefty", type);
  context->set_number(context->ctx, entity, "state", size);
  monster->lefty = true;
  const qa_monster_mission *mission = game->mission(game->ctx, entity->actor.id);
  if (mission) mission->spawned(mission->ctx);
  else if (strcmp(context->program, "mg3") == 0 || context->services_cvar(context->services_ctx, "horde") == 0 ||
           (FLAGS(entity->spawnflags) & 4) == 0)
    game->total_monsters++;
  if (strcmp(context->program, "mg3") == 0 && qa_q1am_actor_text(entity, "health_target")[0] != '\0')
    entity->max_health = game->health(game->ctx, entity->actor.id);
  char name[220];
  if ((FLAGS(entity->spawnflags) & 4) != 0) {
    (void)snprintf(name, sizeof name, "%s:start", prefix(monster));
    entity->use = game->named_use(game->ctx, entity, name);
    return;
  }
  (void)snprintf(name, sizeof name, "%s:monster_start", prefix(monster));
  game->schedule(game->ctx, entity,
                 fmax(0, entity->next_think) + game->random(game->ctx) * 0.5 - game->time,
                 game->named_action(game->ctx, entity, name));
}

void qa_q1am_start_mg3_monster(qa_q1am_monster *monster, qa_q1am_context *context) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  const char *name = prefix(monster);
  double type = qa_q1am_actor_number(entity, "lefty");
  double size = qa_q1am_actor_number(entity, "state");
  qa_bounds bounds = size == 1 ? small_bounds : large_bounds;
  entity->solid = "slidebox";
  entity->movement = "step";
  (void)snprintf(entity->model, sizeof entity->model, "%s", qa_q1am_actor_text(entity, "mdl"));
  game->set_bounds(game->ctx, entity, bounds);
  if (type == 1) {
    qa_vec3 start = qa_add3(qa_q1am_monster_origin(monster), (qa_vec3){0, 0, 1});
    qa_q1am_trace_request request = {start, qa_sub3(start, (qa_vec3){0, 0, 256}), bounds, true, entity->actor.id, true};
    qa_q1am_trace trace = game->trace(game->ctx, &request);
    qa_q1am_body_patch patch = {0};
    patch.has_origin = true;
    patch.origin = trace.fraction < 1 && !trace.all_solid ? trace.end : start;
    patch.has_ground = true;
    patch.ground_null = !trace.has_actor;
    patch.ground = trace.actor;
    game->set_body(game->ctx, entity, &patch);
    if (trace.fraction < 1 && !trace.all_solid)
      entity->movement_flags = (double)(FLAGS(entity->movement_flags) | 512);
  }
  game->walk_move(game->ctx, entity->actor, 0, 0);
  char callback[220];
  if ((FLAGS(entity->spawnflags) & 4) != 0) {
    qa_q1am_actor *death = game->create(game->ctx, "teledeath");
    death->has_owner = true;
    death->owner = entity->actor.id;
    death->solid = "trigger";
    death->touch = game->named_touch(game->ctx, death, "tdeath_touch");
    qa_q1am_body_patch patch = {0};
    patch.has_origin = true;
    patch.origin = qa_q1am_monster_origin(monster);
    patch.has_bounds = true;
    patch.bounds.min = qa_sub3(bounds.min, (qa_vec3){1, 1, 1});
    patch.bounds.max = qa_add3(bounds.max, (qa_vec3){1, 1, 1});
    game->set_body(game->ctx, death, &patch);
    game->link(game->ctx, death);
    game->schedule(game->ctx, death, 0.2, game->named_action(game->ctx, death, "SUB_Remove"));
    if ((FLAGS(entity->spawnflags) & 16) != 0)
      game->effect(game->ctx, "teleport", qa_q1am_monster_origin(monster), false, (qa_actor_id){0}, 1);
  }
  entity->aimed_damage = true;
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->ideal_yaw = game->body(game->ctx, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 20;
  context->set_vector(context->ctx, entity, "view_ofs", (qa_vec3){0, 0, 25});
  (void)snprintf(callback, sizeof callback, "%s:monster_use", name);
  entity->use = game->named_use(game->ctx, entity, callback);
  entity->movement_flags = (double)(FLAGS(entity->movement_flags) | 32 | (type == 2 ? 1 : type == 3 ? 2 : 0));
  game->combat_set_team(game->ctx, entity->actor, "q1:monsters");
  game->link(game->ctx, entity);
  const qa_monster_mission *mission = game->mission(game->ctx, entity->actor.id);
  if (mission) {
    mission->started(mission->ctx);
    qa_q1am_monster_update_route(monster);
    qa_q1am_monster_delay(monster, entity->next_think - game->time + game->random(game->ctx) * 0.5);
    return;
  }
  size_t count = 0;
  qa_q1am_actor **targets = entity->target[0] == '\0' ? NULL : game->find(game->ctx, entity->target, &count);
  if ((FLAGS(entity->spawnflags) & 32) != 0 && entity->target[0] != '\0') {
    size_t alive = 0;
    for (size_t i = 0; i < count; i++)
      if (qa_q1am_actor_damageable(game, targets[i])) alive++;
    if (alive > 0) {
      size_t wanted = (size_t)floor(game->random(game->ctx) * (double)alive);
      qa_q1am_actor *chosen = NULL;
      for (size_t i = 0, seen = 0; i < count; i++)
        if (qa_q1am_actor_damageable(game, targets[i]) && seen++ == wanted) chosen = targets[i];
      if (chosen) {
        qa_actor_id id = chosen->actor.id;
        free(targets);
        monster->ops.found(monster, id);
        return;
      }
    }
  }
  qa_q1am_actor *target = count > 0 ? targets[0] : NULL;
  qa_q1am_actor_set_ref(entity, "movetarget", target != NULL, target ? target->actor.id : (qa_actor_id){0});
  qa_q1am_actor_set_ref(entity, "goalentity", target != NULL, target ? target->actor.id : (qa_actor_id){0});
  (void)snprintf(monster->state->path, sizeof monster->state->path, "%s", target ? entity->target : "");
  if (target)
    entity->ideal_yaw = qa_q1am_yaw_for(qa_sub3(game->body(game->ctx, target).origin, qa_q1am_monster_origin(monster)));
  bool path_corner = target && strcmp(target->classname, "path_corner") == 0;
  if (path_corner && (FLAGS(entity->spawnflags) & 4096) == 0) {
    monster->ops.play(monster, monster->spec.walk);
  } else {
    monster->state->pause_until = 99999999;
    monster->ops.play(monster, monster->spec.stand);
    if (path_corner) {
      (void)snprintf(callback, sizeof callback, "%s:walk", name);
      entity->use = game->named_use(game->ctx, entity, callback);
    }
  }
  free(targets);
  if ((FLAGS(entity->spawnflags) & 4) == 0) {
    qa_q1am_monster_delay(monster, entity->next_think - game->time + game->random(game->ctx) * 0.5);
    return;
  }
  if (strcmp(context->program, "mg3") != 0) {
    size_t total = game->entity_count(game->ctx);
    for (size_t i = 0; i < total; i++) {
      qa_q1am_actor *candidate = game->entity_at(game->ctx, i);
      if (candidate && strcmp(candidate->classname, "horde_manager") == 0) {
        game->total_monsters++;
        break;
      }
    }
  }
  if ((FLAGS(entity->spawnflags) & 8) != 0)
    monster->ops.use(monster, entity->has_activator, entity->activator);
}

bool qa_q1am_mg3_monster_activator(qa_q1am_game *game, bool has, qa_actor_id activator, qa_actor_id *out) {
  if (has && game->is_player(game->ctx, true, activator)) {
    *out = activator;
    return true;
  }
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  bool found = false;
  for (size_t i = 0; i < count && !found; i++)
    if (game->health(game->ctx, players[i]) > 0) {
      *out = players[i];
      found = true;
    }
  free(players);
  return found;
}

typedef struct startup_ctx {
  qa_q1am_monster_lookup lookup;
  char name[200];
} startup_ctx;
static void startup_start_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity,
                              bool has_other, qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)game; (void)has_other; (void)other;
  startup_ctx *state = ctx;
  entity->has_activator = has_activator;
  entity->activator = activator;
  qa_q1am_monster *monster = state->lookup.fn(state->lookup.ctx, entity);
  monster->ops.start(monster);
}
static void startup_walk_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity,
                             bool has_other, qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)has_other; (void)other; (void)has_activator; (void)activator;
  startup_ctx *state = ctx;
  qa_q1am_monster *value = state->lookup.fn(state->lookup.ctx, entity);
  if (game->health(game->ctx, entity->actor.id) <= 0 || value->state->has_enemy) {
    entity->use.present = false;
    return;
  }
  char callback[240];
  (void)snprintf(callback, sizeof callback, "%s:monster_use", state->name);
  entity->use = game->named_use(game->ctx, entity, callback);
  value->state->pause_until = 0;
  value->ops.play(value, value->spec.walk);
}
void qa_q1am_register_mg3_monster_startup(qa_q1am_game *game, const char *name,
                                          qa_q1am_monster_lookup monster) {
  start_names *names = names_for(game, true);
  if (names->count == names->cap) {
    size_t cap = names->cap ? names->cap * 2 : 8;
    char (*next)[200] = realloc(names->names, cap * sizeof *next);
    if (!next) qa_range_error("MG3 startup allocation failed");
    names->names = next;
    names->cap = cap;
  }
  (void)snprintf(names->names[names->count++], 200, "%s:start", name);
  startup_ctx *state = malloc(sizeof *state);
  if (!state) qa_range_error("MG3 startup allocation failed");
  state->lookup = monster;
  (void)snprintf(state->name, sizeof state->name, "%s", name);
  char callback[240];
  qa_q1am_handlers handlers = {0};
  handlers.ctx = state;
  handlers.use = startup_start_use;
  (void)snprintf(callback, sizeof callback, "%s:start", name);
  game->named_register(game->ctx, callback, handlers);
  handlers.use = startup_walk_use;
  (void)snprintf(callback, sizeof callback, "%s:walk", name);
  game->named_register(game->ctx, callback, handlers);
}
bool qa_q1am_waiting_mg3_monster(qa_q1am_game *game, qa_q1am_actor *entity) {
  const char *name = game->callback_name(game->ctx, entity->use.present, &entity->use);
  if (name == NULL) return false;
  start_names *names = names_for(game, false);
  if (!names) return false;
  for (size_t i = 0; i < names->count; i++)
    if (strcmp(names->names[i], name) == 0) return true;
  return false;
}
