/* Port of src/content/q1/addons/monsters/startup.ts.
 * quakec_{mg1,mg3}/monsters.qc InitMonster, StartMonster, monster_begin_walking. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/startup.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/spawns.h"
#include "support.h"

#define FLAGS QA_Q1AM_FLAGS

static const qa_bounds small_bounds = {{-16, -16, -24}, {16, 16, 32}};
static const qa_bounds large_bounds = {{-32, -32, -24}, {32, 32, 64}};

/* One registerMg3MonsterStartup closure environment. */
typedef struct startup_registration {
  char *name;
  qa_q1_monster_lookup_fn monster;
  void *monster_ctx;
} startup_registration;
/* TS WeakMap<Q1EntityServices, Set<string>> (the set holds `${name}:start`). */
typedef struct start_names {
  qa_q1_services *game;
  char **names; size_t count, capacity;
  startup_registration **registrations; size_t registration_count, registration_capacity;
  struct start_names *next;
} start_names;
static start_names *all_start_names;

static start_names *names_for(qa_q1_services *game) {
  for (start_names *entry = all_start_names; entry != NULL; entry = entry->next)
    if (entry->game == game) return entry;
  return NULL;
}
static char *dup_text(const char *text) {
  const size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static const char *prefix(const qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL ? monster->source.callback_prefix : "base";
}
static bool classname_is(const qa_q1_actor *entity, const char *classname) {
  return entity != NULL && entity->classname != NULL && strcmp(entity->classname, classname) == 0;
}
static qa_q1_bound_use named_use(qa_q1_services *game, qa_q1_actor *entity, const char *name, const char *suffix) {
  char callback[256];
  (void)snprintf(callback, sizeof callback, "%s:%s", name, suffix);
  return qa_q1_callback_registry_use(game->named, entity, callback);
}

void qa_q1am_init_mg3_monster(qa_q1_base_monster *monster, qa_q1ad_context *context,
                              const char *model, double type, double size) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (game->options.deathmatch != 0 || qa_q1ad_removed_for_runes(context, entity) ||
      qa_q1ad_removed_outside_coop(context, entity, true)) {
    if (qa_q1_live(game, entity)) qa_q1_remove(game, entity);
    return;
  }
  entity->movement_flags = FLAGS(entity->movement_flags) | 16384;
  qa_q1am_field_text(entity, "mdl", model);
  qa_q1ad_set_number(context, entity, "lefty", type);
  qa_q1ad_set_number(context, entity, "state", size);
  monster->lefty = true;
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL) mission->spawned(mission->ctx);
  else if (context->program == QA_Q1AD_MG3 || context->services.cvar(context->services.ctx, "horde") == 0 ||
           (FLAGS(entity->spawnflags) & 4) == 0)
    game->total_monsters++;
  if (context->program == QA_Q1AD_MG3 && qa_q1_actor_text(entity, "health_target")[0] != '\0')
    entity->max_health = qa_q1_health(game, entity->actor.id);
  if ((FLAGS(entity->spawnflags) & 4) != 0) {
    entity->use = named_use(game, entity, prefix(monster), "start");
    return;
  }
  char callback[256];
  (void)snprintf(callback, sizeof callback, "%s:monster_start", prefix(monster));
  const double first = qa_js_max(0, entity->next_think);
  const double delay = first + qa_q1am_random(game) * 0.5 - game->time;
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, callback));
}

void qa_q1am_start_mg3_monster(qa_q1_base_monster *monster, qa_q1ad_context *context) {
  qa_vec3 a;
  qa_vec3 b;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  const char *name = prefix(monster);
  const double type = qa_q1_actor_number(entity, "lefty"), size = qa_q1_actor_number(entity, "state");
  const qa_bounds bounds = size == 1 ? small_bounds : large_bounds;
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1ad_set_text(&entity->model, qa_q1_actor_text(entity, "mdl"));
  qa_q1_set_bounds(game, entity, bounds);
  if (type == 1) {
    const qa_vec3 start = qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 1});
    const qa_q1_trace trace = qa_q1am_trace(game, start, qa_sub3(start, (qa_vec3){0, 0, 256}), bounds,
                                            &entity->actor.id, true);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_origin = true;
    patch.origin = trace.fraction < 1 && !trace.all_solid ? trace.end : start;
    patch.has_ground = true;
    patch.ground = trace.has_actor ? &trace.actor : NULL;
    qa_q1_set_body(game, entity, &patch);
    if (trace.fraction < 1 && !trace.all_solid) entity->movement_flags = FLAGS(entity->movement_flags) | 512;
  }
  (void)qa_q1am_walk_move(game, entity->actor, 0, 0);
  if ((FLAGS(entity->spawnflags) & 4) != 0) {
    (void)qa_q1_spawn_teledeath(game, qa_q1_base_monster_origin(monster), entity->actor.id);
    if ((FLAGS(entity->spawnflags) & 16) != 0) (void)qa_q1_spawn_teleport_fog(game, qa_q1_base_monster_origin(monster));
  }
  entity->aimed_damage = true;
  qa_q1_actor_set_damageable(entity, true);
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  if (entity->yaw_speed == 0 || isnan(entity->yaw_speed)) entity->yaw_speed = 20; /* TS ||= */
  qa_q1ad_set_vector(context, entity, "view_ofs", (qa_vec3){0, 0, 25});
  entity->use = named_use(game, entity, name, "monster_use");
  entity->movement_flags = FLAGS(entity->movement_flags) | 32 | (type == 2 ? 1 : type == 3 ? 2 : 0);
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = "q1:monsters";
  qa_gameplay_authority_set_traits(game->host.combat, entity->actor, traits);
  qa_q1_link(game, entity);
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL) {
    mission->started(mission->ctx);
    qa_q1_base_monster_update_route(monster);
    qa_q1_base_monster_delay(monster, entity->next_think - game->time + qa_q1am_random(game) * 0.5);
    return;
  }
  size_t count = 0;
  qa_q1_actor **targets = entity->target[0] == '\0' ? NULL : qa_q1am_find(game, entity->target, &count);
  if ((FLAGS(entity->spawnflags) & 32) != 0 && entity->target[0] != '\0') {
    /* TS alive = targets.filter(target => target.damageable) */
    size_t alive = 0;
    for (size_t i = 0; i < count; i++)
      if (qa_q1_actor_damageable(targets[i])) alive++;
    const double index = floor(qa_q1am_random(game) * (double)alive);
    if (index < (double)alive) {
      qa_actor_id chosen = {0};
      for (size_t i = 0, seen = 0; i < count; i++)
        if (qa_q1_actor_damageable(targets[i]) && (double)seen++ == index) chosen = targets[i]->actor.id;
      free(targets);
      qa_q1_base_monster_found(monster, chosen);
      return;
    }
  }
  qa_q1_actor *target = count > 0 ? targets[0] : NULL;
  qa_q1am_reference(entity, "movetarget", target == NULL ? NULL : &target->actor.id);
  qa_q1am_reference(entity, "goalentity", target == NULL ? NULL : &target->actor.id);
  qa_q1ad_set_text(&monster->state->path, target == NULL ? "" : entity->target);
  if (target != NULL)
    entity->ideal_yaw = qa_q1_yaw_for((a = qa_q1_get_body(game, target).origin, b = qa_q1_base_monster_origin(monster), qa_sub3(a, b)));
  const bool path_corner = classname_is(target, "path_corner");
  free(targets);
  if (path_corner && (FLAGS(entity->spawnflags) & 4096) == 0) {
    qa_q1_base_monster_play(monster, spec->walk);
  } else {
    monster->state->pause_until = 99999999;
    qa_q1_base_monster_play(monster, spec->stand);
    if (path_corner) entity->use = named_use(game, entity, name, "walk");
  }
  if ((FLAGS(entity->spawnflags) & 4) == 0) {
    qa_q1_base_monster_delay(monster, entity->next_think - game->time + qa_q1am_random(game) * 0.5);
    return;
  }
  if (context->program != QA_Q1AD_MG3)
    for (size_t i = 0; i < game->entity_count; i++)
      if (classname_is(game->entities[i], "horde_manager")) { game->total_monsters++; break; }
  if ((FLAGS(entity->spawnflags) & 8) != 0) {
    const qa_actor_id activator = entity->activator;
    qa_q1_base_monster_use(monster, entity->has_activator ? &activator : NULL);
  }
}

bool qa_q1am_mg3_monster_activator(qa_q1_services *game, const qa_actor_id *activator, qa_actor_id *out) {
  if (activator != NULL && qa_q1_is_player(game, activator)) {
    *out = *activator;
    return true;
  }
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_q1_health(game, players[i]) > 0) {
      *out = players[i];
      return true;
    }
  return false;
}

static void start_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)game; (void)other; /* TS (_game, entity, _other, activator) */
  const startup_registration *registration = ctx;
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
  qa_q1_base_monster_start(registration->monster(registration->monster_ctx, entity));
}
static void walk_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)other; (void)activator; /* TS (_game, entity) */
  const startup_registration *registration = ctx;
  qa_q1_base_monster *value = registration->monster(registration->monster_ctx, entity);
  if (strncmp(registration->name, "mg1:", 4) != 0 &&
      (qa_q1_health(game, entity->actor.id) <= 0 || value->state->has_enemy)) {
    memset(&entity->use, 0, sizeof entity->use);
    return;
  }
  entity->use = named_use(game, entity, registration->name, "monster_use");
  value->state->pause_until = 0;
  qa_q1_base_monster_play(value, value->spec->walk);
}
static void *grow(void *items, size_t *capacity, size_t size) {
  const size_t next = *capacity != 0 ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}
void qa_q1am_register_mg3_monster_startup(qa_q1_services *game, const char *name,
                                          qa_q1_monster_lookup_fn monster, void *monster_ctx) {
  start_names *names = names_for(game);
  if (names == NULL) {
    names = calloc(1, sizeof *names);
    if (names == NULL) qa_range_error("out of memory");
    names->game = game;
    names->next = all_start_names;
    all_start_names = names;
  }
  char start[256];
  (void)snprintf(start, sizeof start, "%s:start", name);
  bool present = false;
  for (size_t i = 0; i < names->count; i++) present = present || strcmp(names->names[i], start) == 0;
  if (!present) {
    if (names->count == names->capacity) names->names = grow(names->names, &names->capacity, sizeof *names->names);
    names->names[names->count++] = dup_text(start);
  }
  startup_registration *registration = malloc(sizeof *registration);
  if (registration == NULL) qa_range_error("out of memory");
  registration->name = dup_text(name);
  registration->monster = monster;
  registration->monster_ctx = monster_ctx;
  if (names->registration_count == names->registration_capacity)
    names->registrations = grow(names->registrations, &names->registration_capacity, sizeof *names->registrations);
  names->registrations[names->registration_count++] = registration;
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = registration;
  handlers.use = start_use;
  qa_q1_callback_registry_register(game->named, start, handlers);
  char walk[256];
  (void)snprintf(walk, sizeof walk, "%s:walk", name);
  handlers.use = walk_use;
  qa_q1_callback_registry_register(game->named, walk, handlers);
}
bool qa_q1am_waiting_mg3_monster(qa_q1_services *game, qa_q1_actor *entity) {
  const char *name = qa_q1_use_name(entity->use);
  if (name == NULL) return false;
  const start_names *names = names_for(game);
  if (names == NULL) return false;
  for (size_t i = 0; i < names->count; i++)
    if (strcmp(names->names[i], name) == 0) return true;
  return false;
}
void qa_q1am_release_mg3_monster_startup(qa_q1_services *game) {
  for (start_names **link = &all_start_names; *link != NULL; link = &(*link)->next) {
    start_names *names = *link;
    if (names->game != game) continue;
    *link = names->next;
    for (size_t i = 0; i < names->count; i++) free(names->names[i]);
    for (size_t i = 0; i < names->registration_count; i++) {
      free(names->registrations[i]->name);
      free(names->registrations[i]);
    }
    free(names->names);
    free(names->registrations);
    free(names);
    return;
  }
}
