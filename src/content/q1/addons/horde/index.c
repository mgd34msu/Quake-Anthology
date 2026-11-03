/* Port of src/content/q1/addons/horde/index.ts (quakec_mg1/horde.qc, combat.qc and client.qc).
 * Copyright (C) 1996-2022 id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/horde/index.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/horde/loot.h"
#include "qa/content/q1/addons/horde/squads.h"
#include "qa/content/q1/addons/monsters/ordinary/index.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static double fr(double value) { return (double)(float)value; }

static qa_q1_bound_action named_action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(game->named, entity, name);
}

/* [...game.entities.values()]: the map can change while the caller works. */
static qa_q1_actor **entity_snapshot(qa_q1_services *game, size_t *count) {
  *count = game->entity_count;
  qa_q1_actor **copy = malloc((*count == 0 ? 1 : *count) * sizeof *copy);
  if (copy == NULL) qa_range_error("out of memory");
  if (*count != 0) memcpy(copy, game->entities, *count * sizeof *copy);
  return copy;
}

/* game.host.players(): a fresh array in TS. */
static qa_actor_id *player_snapshot(qa_q1_services *game, size_t *count) {
  const qa_actor_id *players = game->host.players(game->host.ctx, count);
  qa_actor_id *copy = malloc((*count == 0 ? 1 : *count) * sizeof *copy);
  if (copy == NULL) qa_range_error("out of memory");
  if (*count != 0) memcpy(copy, players, *count * sizeof *copy);
  return copy;
}

static bool is_class(const qa_q1_actor *entity, const char *classname) {
  return strcmp(entity->classname, classname) == 0;
}

qa_q1_actor *qa_q1ad_horde_manager(qa_q1ad_horde *horde) {
  qa_q1_services *game = horde->context->game;
  for (size_t i = 0; i < game->entity_count; i++)
    if (is_class(game->entities[i], "horde_manager")) return game->entities[i];
  return NULL;
}

void qa_q1ad_horde_schedule(qa_q1ad_horde *horde, qa_q1_actor *entity, const char *name, double delay) {
  qa_q1_services *game = horde->context->game;
  char full[96];
  (void)snprintf(full, sizeof full, "mg1:horde:%s", name);
  qa_q1_schedule(game, entity, delay, named_action(game, entity, full));
}

size_t qa_q1ad_horde_living_players(qa_q1ad_horde *horde, qa_actor_id *out, size_t capacity) {
  qa_q1_services *game = horde->context->game;
  size_t count = 0, living = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_q1_health(game, players[i]) > 0) {
      if (living < capacity) out[living] = players[i];
      living++;
    }
  return living;
}

size_t qa_q1ad_horde_living_monsters(qa_q1ad_horde *horde, qa_q1_actor **out, size_t capacity) {
  qa_q1_services *game = horde->context->game;
  size_t living = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (strcmp(qa_q1_actor_text(entity, "category"), "monster") == 0 && !is_class(entity, "monster_zombie") &&
        qa_q1_health(game, entity->actor.id) > 0) {
      if (living < capacity) out[living] = entity;
      living++;
    }
  }
  return living;
}

bool qa_q1ad_horde_target(qa_q1ad_horde *horde, qa_actor_id *out) {
  qa_q1_services *game = horde->context->game;
  size_t count = qa_q1ad_horde_living_players(horde, NULL, 0);
  if (count == 0) return false;
  double roll = qa_q1_host_random(game) * (double)count;
  double index = 0;
  size_t player_count = 0;
  qa_actor_id *players = player_snapshot(game, &player_count);
  bool found = false;
  for (size_t i = 0; i < player_count && !found; i++) {
    if (qa_q1_health(game, players[i]) > 0) index++;
    if (roll <= index && !horde->services.no_target(horde->services.ctx, players[i])) {
      *out = players[i];
      found = true;
    }
  }
  free(players);
  return found;
}

bool qa_q1ad_horde_blocked(qa_q1ad_horde *horde, qa_q1_actor *point) {
  if ((qa_js_to_int32(point->spawnflags) & 2) != 0) return false;
  qa_q1_services *game = horde->context->game;
  qa_q1_body source = qa_q1_get_body(game, point);
  qa_vec3 min = qa_q1_vadd(source.origin, source.bounds.min), max = qa_q1_vadd(source.origin, source.bounds.max);
  size_t count = qa_q1ad_horde_living_players(horde, NULL, 0);
  qa_actor_id *living = malloc((count == 0 ? 1 : count) * sizeof *living);
  if (living == NULL) qa_range_error("out of memory");
  (void)qa_q1ad_horde_living_players(horde, living, count);
  bool blocked = false;
  for (size_t i = 0; i < count && !blocked; i++) {
    if (horde->services.dead_flag(horde->services.ctx, living[i]) > 0) continue;
    qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, living[i]);
    if (!body.has_value) continue;
    qa_vec3 low = qa_q1_vadd(body.value.origin, body.value.bounds.min);
    qa_vec3 high = qa_q1_vadd(body.value.origin, body.value.bounds.max);
    /* Official CheckBlockedSpawn repeats the Y comparison and does not test Z. */
    blocked = high.x > min.x && low.x < max.x && high.y > min.y && low.y < max.y;
  }
  free(living);
  return blocked;
}

static size_t spawn_points(qa_q1_services *game, const char *kind, qa_q1_actor **out) {
  char classname[64];
  if (strcmp(kind, "normal") == 0) (void)snprintf(classname, sizeof classname, "info_monster_start");
  else (void)snprintf(classname, sizeof classname, "info_monster_start_%s", kind);
  size_t count = 0;
  for (size_t i = 0; i < game->entity_count; i++)
    if (is_class(game->entities[i], classname)) out[count++] = game->entities[i];
  return count;
}

static bool spawn_valid(qa_q1ad_horde *horde, qa_q1_actor *point) {
  return horde->context->game->time > point->wait && (qa_js_to_int32(point->spawnflags) & 1) == 0 &&
         !qa_q1ad_horde_blocked(horde, point);
}

static bool any_valid(qa_q1ad_horde *horde, qa_q1_actor **candidates, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (spawn_valid(horde, candidates[i])) return true;
  return false;
}

qa_q1_actor *qa_q1ad_horde_find_spawn(qa_q1ad_horde *horde, const char *type) {
  qa_q1_services *game = horde->context->game;
  qa_q1_actor **candidates = malloc((game->entity_count == 0 ? 1 : game->entity_count) * sizeof *candidates);
  if (candidates == NULL) qa_range_error("out of memory");
  size_t count = spawn_points(game, type, candidates);
  qa_q1_actor *chosen = NULL;
  if (!any_valid(horde, candidates, count)) {
    if (strcmp(type, "normal") == 0) { free(candidates); return NULL; }
    count = spawn_points(game, "normal", candidates);
  }
  if (any_valid(horde, candidates, count)) {
    double roll = (double)count * qa_q1_host_random(game);
    for (size_t i = 0; i < count && chosen == NULL; i++)
      if ((double)i + 1 >= roll && spawn_valid(horde, candidates[i])) chosen = candidates[i];
    for (size_t i = 0; i < count && chosen == NULL; i++)
      if (spawn_valid(horde, candidates[i])) chosen = candidates[i];
  }
  free(candidates);
  return chosen;
}

static double game_random(void *ctx) { return qa_q1_host_random(ctx); }

static double bit_or(double value, int32_t bits) { return (double)(qa_js_to_int32(value) | bits); }

qa_q1_actor *qa_q1ad_horde_spawn_monster(qa_q1ad_horde *horde, const char *kind, qa_vec3 origin, qa_vec3 angles,
                                         qa_q1_actor *owner) {
  qa_q1ad_context *context = horde->context;
  qa_q1_services *game = context->game;
  char classname[64];
  if (strcmp(kind, "grunt") == 0) (void)snprintf(classname, sizeof classname, "monster_army");
  else if (strcmp(kind, "hellknight") == 0) (void)snprintf(classname, sizeof classname, "monster_hell_knight");
  else if (strcmp(kind, "demon") == 0) (void)snprintf(classname, sizeof classname, "monster_demon1");
  else (void)snprintf(classname, sizeof classname, "monster_%s", kind);
  qa_q1_actor *entity = qa_q1_create(game, classname, NULL, NULL);
  qa_q1_body_patch placed = {.has_origin = true, .origin = origin, .has_angles = true, .angles = angles};
  qa_q1_set_body(game, entity, &placed);
  qa_q1_spawn_entity(game, entity);
  qa_q1_base_monster *controller = qa_q1am_ordinary_addon_monster(context, entity);
  qa_q1_monster *monster = entity->monster;
  if (controller == NULL) qa_range_error("Horde has no native monster controller for %s", kind);
  qa_q1_cancel(game, entity);
  if (controller != NULL) {
    char model[96];
    (void)snprintf(model, sizeof model, "progs/%s.mdl", controller->spec->model);
    qa_q1ad_set_text(&entity->model, model);
    entity->solid = "slidebox";
    entity->movement = "step";
    entity->aimed_damage = true;
    qa_q1_set_bounds(game, entity, controller->spec->bounds);
    qa_q1ad_set_vector(context, entity, "view_ofs", (qa_vec3){0, 0, 25});
    qa_gameplay_authority_set_traits(game->host.combat, entity->actor,
                                     (qa_combat_trait_changes){.has_team = true, .team = "q1:monsters"});
    if (strcmp(kind, "wizard") == 0) entity->movement_flags = bit_or(entity->movement_flags, 1);
  }
  double offset = strcmp(kind, "demon") == 0 ? 48
      : strcmp(kind, "ogre") == 0 || strcmp(kind, "shambler") == 0 || strcmp(kind, "shalrath") == 0 ||
              strcmp(kind, "wizard") == 0 || strcmp(kind, "zombie") == 0
          ? 32 : 24;
  qa_vec3 position = qa_q1_vadd(origin, (qa_vec3){0, 0, offset + 1});
  if (strcmp(kind, "wizard") != 0) {
    qa_q1_trace_request request = {.start = position, .end = qa_q1_vsub(position, (qa_vec3){0, 0, 256}),
                                   .bounds = qa_q1_get_body(game, entity).bounds, .has_ignore = true,
                                   .ignore = entity->actor.id, .monsters = true};
    qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (trace.fraction < 1 && !trace.all_solid) {
      position = trace.end;
      qa_q1_body_patch ground = {.has_ground = true, .ground = trace.has_actor ? &trace.actor : NULL};
      qa_q1_set_body(game, entity, &ground);
      entity->movement_flags = bit_or(entity->movement_flags, 512);
    }
  }
  qa_q1_set_origin(game, entity, position);
  (void)game->host.walk_move(game->host.ctx, entity->actor, 0, 0);
  (void)qa_q1_actor_field_set(entity, "category", "monster");
  qa_q1_actor_set_damageable(entity, true);
  entity->has_owner = true;
  entity->owner = owner->actor.id;
  entity->yaw_speed = 20;
  entity->movement_flags = bit_or(entity->movement_flags, 32);
  qa_actor_id target;
  bool has_target = qa_q1ad_horde_target(horde, &target);
  if (controller != NULL) qa_q1_base_monster_set_enemy(controller, has_target ? &target : NULL);
  else if (monster != NULL) {
    monster->has_enemy = has_target;
    if (has_target) monster->enemy = target;
  }
  const char *source_die = qa_q1_die_name(entity->die);
  if (source_die == NULL) qa_range_error("Horde monster has no named source death");
  (void)qa_q1_actor_field_set(entity, "horde.sourceDie", source_die);
  entity->die = qa_q1_callback_registry_die(game->named, entity, "mg1:horde:die");
  if (strcmp(kind, "zombie") == 0) game->total_monsters--;
  qa_q1ad_horde_schedule(horde, entity, "found", 0.1);
  qa_q1_actor *death = qa_q1_create(game, "teledeath", NULL, NULL);
  qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
  death->has_owner = true;
  death->owner = entity->actor.id;
  death->solid = "trigger";
  death->touch = qa_q1_callback_registry_touch(game->named, death, "tdeath_touch");
  qa_q1_body_patch death_patch = {
      .has_origin = true, .origin = position, .has_bounds = true,
      .bounds = {qa_q1_vsub(bounds.min, (qa_vec3){1, 1, 1}), qa_q1_vadd(bounds.max, (qa_vec3){1, 1, 1})}};
  qa_q1_set_body(game, death, &death_patch);
  qa_q1_link(game, death);
  qa_bounds box = qa_q1_get_body(game, death).bounds;
  size_t observed_count = 0;
  const qa_actor_observation *observed = qa_session_actor_registry_observations(game->host.actors, &observed_count);
  qa_actor_id *ids = malloc((observed_count == 0 ? 1 : observed_count) * sizeof *ids);
  if (ids == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < observed_count; i++) ids[i] = observed[i].id;
  for (size_t i = 0; i < observed_count; i++) {
    qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, ids[i]);
    if (!body.has_value) continue;
    qa_bounds mine = {qa_q1_vadd(position, box.min), qa_q1_vadd(position, box.max)};
    qa_bounds theirs = {qa_q1_vadd(body.value.origin, body.value.bounds.min),
                        qa_q1_vadd(body.value.origin, body.value.bounds.max)};
    if (qa_q1_overlaps(mine, theirs))
      (void)qa_actor_callback_table_touch(game->host.callbacks,
                                          (qa_touch_contact){.self = death->actor, .other = ids[i]});
  }
  free(ids);
  qa_q1_schedule(game, death, 0.01, named_action(game, death, "SUB_Remove"));
  return entity;
}

bool qa_q1ad_horde_prepare(qa_q1ad_horde *horde, qa_q1_actor *manager) {
  qa_q1ad_context *context = horde->context;
  qa_q1_services *game = context->game;
  qa_q1ad_set_number(context, manager, "key_spawned", 0);
  double players = (double)qa_q1ad_horde_living_players(horde, NULL, 0);
  if (players < 1) return false;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (is_class(entity, "info_horde_item") && entity->wait == 0)
      qa_q1ad_horde_schedule(horde, entity, "item", qa_q1_host_random(game) * 2);
  }
  double wave = qa_q1_actor_number(manager, "wave") + 1, skill = qa_q1_option_skill(game);
  double level = wave + (skill >= 3 ? 6 : skill >= 2 ? 3 : 0);
  bool army = fmod(level + 2, 3) == 0;
  qa_q1ad_set_number(context, manager, "wave", wave);
  qa_q1ad_set_number(context, manager, "army", army ? 1 : 0);
  double scale = players >= 4 ? 2 : players >= 3 ? 1.5 : players >= 2 ? 1.25 : 1;
  double bosses = qa_q1_actor_number(manager, "bosses");
  if (fmod(level, 3) == 0) bosses = floor((level + 1) / 4);
  else if (skill > 1 && !army && level > 9) bosses = floor((level + 1) / 8);
  double elites = floor((ceil((level - 1) / 3) - floor(bosses / 2)) * scale);
  double fodder = floor((level + 2 - (bosses * 2 + elites)) * scale);
  qa_q1ad_set_number(context, manager, "bosses", bosses);
  qa_q1ad_set_number(context, manager, "elites", elites);
  qa_q1ad_set_number(context, manager, "fodder", fodder);
  qa_q1_actor **targets = malloc((game->entity_count == 0 ? 1 : game->entity_count) * sizeof *targets);
  if (targets == NULL) qa_range_error("out of memory");
  size_t found = qa_q1_find(game, manager->target, targets, game->entity_count);
  for (size_t i = 0; i < found; i++)
    qa_q1_call_use(game, targets[i], targets[i]->use, &manager->actor.id,
                   manager->has_activator ? &manager->activator : NULL);
  free(targets);
  manager->wait = 1;
  return true;
}

void qa_q1ad_horde_spawn_wave(qa_q1ad_horde *horde, qa_q1_actor *manager) {
  qa_q1ad_context *context = horde->context;
  qa_q1_services *game = context->game;
  const char *category = qa_q1_actor_number(manager, "fodder") > 0 ? "fodder"
      : qa_q1_actor_number(manager, "elites") > 0 ? "elites" : "bosses";
  const char *squad = NULL, *type = NULL;
  if (!qa_q1ad_choose_horde_squad(qa_q1_actor_number(manager, "army") != 0, category, game_random, game, &squad,
                                  &type)) {
    qa_q1ad_set_number(context, manager, "bosses", 0);
    qa_q1ad_horde_schedule(horde, manager, "wave", 1);
    return;
  }
  qa_q1_actor *point = qa_q1ad_horde_find_spawn(horde, type);
  if (point == NULL) { qa_q1ad_horde_schedule(horde, manager, "wave", 1); return; }
  point->wait = game->time + 5;
  qa_q1_use_targets(game, point, manager->has_activator ? &manager->activator : NULL);
  qa_q1_body body = qa_q1_get_body(game, point);
  qa_q1ad_squad_spawn spawns[4];
  size_t spawn_count = qa_q1ad_horde_squad(squad, qa_q1_option_skill(game), game_random, game, spawns, 4);
  for (size_t i = 0; i < spawn_count; i++)
    (void)qa_q1ad_horde_spawn_monster(horde, spawns[i].monster, qa_q1_vadd(body.origin, spawns[i].offset),
                                      body.angles, manager);
  qa_q1_effect(game, "teleport", body.origin, NULL, 1);
  qa_q1ad_set_number(context, manager, category, qa_q1_actor_number(manager, category) - 1);
  double left = qa_q1_actor_number(manager, "fodder");
  double right = qa_q1_actor_number(manager, "elites");
  if (left + right +
          qa_q1_actor_number(manager, "bosses") <= 0) {
    manager->wait = 0;
    qa_q1ad_horde_schedule(horde, manager, "check", 30);
    return;
  }
  qa_q1ad_horde_schedule(horde, manager, "wave", 2 + qa_q1_host_random(game));
}

void qa_q1ad_horde_check_wave(qa_q1ad_horde *horde, qa_q1_actor *manager) {
  qa_q1_services *game = horde->context->game;
  if (manager->wait != 0) return;
  qa_q1ad_horde_schedule(horde, manager, "check", 10);
  if (game->killed_monsters + 3 >= game->total_monsters)
    for (size_t i = 0; i < game->entity_count; i++) {
      qa_q1_actor *entity = game->entities[i];
      if (strcmp(qa_q1_actor_text(entity, "category"), "monster") == 0 && qa_q1_health(game, entity->actor.id) <= 0)
        (void)qa_q1_actor_field_set(entity, "category", "");
    }
  double wave = qa_q1_actor_number(manager, "wave");
  if ((double)qa_q1ad_horde_living_monsters(horde, NULL, 0) > (fmod(wave, 3) == 0 || wave < 3 ? 0 : 5)) return;
  size_t player_count = 0;
  qa_actor_id *players = player_snapshot(game, &player_count);
  for (size_t i = 0; i < player_count; i++)
    if (horde->services.dead_flag(horde->services.ctx, players[i]) > 0)
      horde->services.respawn_teammate(horde->services.ctx, players[i]);
  free(players);
  manager->wait = 1;
  if (fmod(wave, 3) == 0) {
    if (qa_q1_actor_number(manager, "key_spawned") == 0) qa_q1ad_horde_get_key(horde, manager);
    qa_q1ad_horde_schedule(horde, manager, "countdown", 20);
    return;
  }
  qa_q1ad_horde_schedule(horde, manager, "countdown", 0);
}

void qa_q1ad_horde_remote_wavecheck(qa_q1ad_horde *horde) {
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL || horde->context->game->has_intermission) return;
  qa_q1ad_horde_check_wave(horde, manager);
}

void qa_q1ad_horde_get_key(qa_q1ad_horde *horde, qa_q1_actor *manager) {
  qa_q1_services *game = horde->context->game;
  qa_q1_actor *first = NULL, *point = NULL;
  double wave = qa_q1_actor_number(manager, "wave");
  int32_t flag = wave <= 3 ? 1 : wave <= 6 ? 2 : wave <= 9 ? 4 : 8;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (!is_class(entity, "info_horde_key")) continue;
    if (first == NULL) first = entity;
    if (point == NULL && (qa_js_to_int32(entity->spawnflags) & flag) != 0) point = entity;
  }
  if (first == NULL) { qa_q1ad_horde_schedule(horde, manager, "countdown", 4); return; }
  qa_q1ad_horde_schedule(horde, point != NULL ? point : first,
                         point == NULL ? wave == 9 ? "gold" : "silver" : flag == 4 ? "gold" : "silver", 0);
}

static qa_item_id key_item(const char *key) { return strcmp(key, "gold") == 0 ? "q1:key/gold" : "q1:key/silver"; }

void qa_q1ad_horde_restore_keys(qa_q1ad_horde *horde, qa_actor_id player) {
  qa_q1_services *game = horde->context->game;
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  qa_owned_actor actor;
  if (manager == NULL || !qa_session_actor_registry_resolve_owned(game->host.actors, player, &actor)) return;
  static const char *const KEYS[2] = {"silver", "gold"};
  for (int i = 0; i < 2; i++) {
    char field[32];
    (void)snprintf(field, sizeof field, "keys_%s", KEYS[i]);
    if (qa_q1_actor_number(manager, field) > 0)
      (void)qa_shared_inventory_table_give(game->host.inventory, actor, key_item(KEYS[i]), 1);
  }
}

void qa_q1ad_horde_change_keys(qa_q1ad_horde *horde, const char *key, int delta) {
  qa_q1_services *game = horde->context->game;
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL) qa_range_error("Horde keys require their manager");
  char field[32];
  (void)snprintf(field, sizeof field, "keys_%s", key);
  double count = qa_q1_actor_number(manager, field) + delta;
  qa_q1ad_set_number(horde->context, manager, field, count);
  if (!((delta == 1 && count == 1) || (delta == -1 && count == 0))) return;
  size_t player_count = 0;
  qa_actor_id *players = player_snapshot(game, &player_count);
  for (size_t i = 0; i < player_count; i++) {
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_owned(game->host.actors, players[i], &actor)) continue;
    if (delta == 1) (void)qa_shared_inventory_table_give(game->host.inventory, actor, key_item(key), 1);
    else (void)qa_shared_inventory_table_consume(game->host.inventory, actor, key_item(key), 1);
  }
  free(players);
}

void qa_q1ad_horde_teammate_killed(qa_q1ad_horde *horde, qa_actor_id attacker) {
  qa_q1ad_context *context = horde->context;
  if (context->services.cvar(context->services.ctx, "horde") == 0) return;
  qa_q1ad_set_player_number(context, attacker, "killtime", 0);
  horde->services.add_score(horde->services.ctx, attacker, -2);
}

bool qa_q1ad_horde_request_respawn(qa_q1ad_horde *horde) {
  if (qa_q1ad_horde_manager(horde) == NULL) return false;
  if (!horde->context->game->options.coop || qa_q1ad_horde_living_players(horde, NULL, 0) == 0)
    qa_q1ad_horde_restart_after_defeat(horde);
  return true;
}

void qa_q1ad_horde_restart_after_defeat(qa_q1ad_horde *horde) {
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL) qa_range_error("Horde restart requires its manager");
  double flags = qa_q1_actor_number(manager, "horde.startingFlags");
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(horde->context->base);
  campaign.write_flags(campaign.ctx, flags);
  const char *map = horde->context->game->map_name;
  horde->services.restart_session(horde->services.ctx, map != NULL ? map : "", flags);
}

/** MonsterFade and MonsterFade2: a settled corpse waits 10 to 15 seconds, then loses a unit of alpha per second and is removed. */
static void fade_corpses(qa_q1ad_horde *horde) {
  qa_q1ad_context *context = horde->context;
  qa_q1_services *game = context->game;
  if ((game->time < horde->last_corpse_scan + 0.1 && game->time >= horde->last_corpse_scan) ||
      context->services.cvar(context->services.ctx, "horde") == 0)
    return;
  horde->last_corpse_scan = game->time;
  size_t count = 0;
  qa_q1_actor **entities = entity_snapshot(game, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *entity = entities[i];
    double fade_at = qa_q1_actor_number(entity, "horde.fadeAt");
    if (fade_at <= 0 || game->time < fade_at) continue;
    double alpha = fr(1 - (game->time - fade_at));
    if (alpha <= 0) qa_q1_remove(game, entity);
    else qa_q1ad_alpha(context, entity, alpha);
  }
  free(entities);
}

/* --- constructor closures --- */
static bool kill_count_rule(void *ctx, qa_q1_base_monster *monster) {
  qa_q1ad_horde *horde = ctx;
  return qa_q1ad_horde_manager(horde) == NULL || !is_class(monster->entity, "monster_zombie");
}

static double attack_delay(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_context *context = horde->context;
  if (strcmp(player->weapon, "axe") != 0 || context->services.cvar(context->services.ctx, "horde") == 0) return delay;
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  if ((qa_js_to_int32(campaign.read_flags(campaign.ctx)) & 4) == 0) return delay;
  double chain = qa_q1ad_player_number(context, player->actor.id, "axe_hit_chain");
  bool chop = chain >= 2 && game->time < qa_q1ad_player_number(context, player->actor.id, "axe_hit_chain_time");
  player->weapon_animation_base = chop ? 1 : 5;
  return chop ? 0.8 : chain > 1 ? 0.6 : 0.4;
}

static void sound_talk(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_sound(game, entity->actor.id, "misc/talk.wav", "voice", 0, 1);
}

static void head_fade(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_context *context = horde->context;
  if (context->services.cvar(context->services.ctx, "horde") == 0) { qa_q1_cancel(game, entity); return; }
  if (qa_q1_actor_number(entity, "alpha") == 0) qa_q1ad_alpha(context, entity, 1);
  qa_q1ad_horde_schedule(horde, entity, "head_fade_step", 10 + qa_q1_host_random(game) * 5);
}

static void head_fade_step(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  if (qa_q1_actor_number(entity, "alpha") <= 0) { qa_q1_remove(game, entity); return; }
  qa_q1ad_alpha(horde->context, entity, fr(qa_q1_actor_number(entity, "alpha") - horde->context->frame_time));
  qa_q1ad_horde_schedule(horde, entity, "head_fade_step", 0);
}

static void horde_set(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* handler signature */
  qa_q1ad_horde_schedule(ctx, entity, "countdown", 1);
}

static void countdown(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  if (!qa_q1ad_horde_prepare(horde, entity)) return;
  qa_q1ad_broadcast(horde->context, "3");
  sound_talk(game, entity);
  qa_q1ad_horde_schedule(horde, entity, "countdown2", 1);
}

static void countdown2(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_broadcast(horde->context, "2");
  sound_talk(game, entity);
  qa_q1ad_horde_schedule(horde, entity, "countdown3", 1);
}

static void countdown3(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_broadcast(horde->context, "1");
  sound_talk(game, entity);
  qa_q1ad_horde_schedule(horde, entity, "fight", 1);
}

static void fight(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_broadcast(horde->context,
                    fmod(qa_q1_actor_number(entity, "wave"), 3) == 0 ? "$qc_horde_boss_wave" : "$qc_horde_fight");
  qa_q1ad_horde_spawn_wave(horde, entity);
  sound_talk(game, entity);
}

static void wave_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* handler signature */
  qa_q1ad_horde_spawn_wave(ctx, entity);
}

static void check_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* handler signature */
  qa_q1ad_horde_check_wave(ctx, entity);
}

static void check_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator; /* TS handler ignores them */
  qa_q1ad_horde_check_wave(ctx, entity);
}

static void point_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator; /* TS handler ignores them */
  entity->spawnflags = (double)(qa_js_to_int32(entity->spawnflags) ^ 1);
}

static void door_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  qa_q1ad_horde *horde = ctx;
  if (qa_q1ad_horde_manager(horde) == NULL) {
    qa_q1_call_touch(game, entity, qa_q1_callback_registry_touch(game->named, entity, "door_touch"), other, normal,
                     surface);
    return;
  }
  if (!qa_q1_is_player(game, &other)) return;
  qa_q1_actor *master = entity->door_group_count > 0 ? entity->door_group[0] : entity;
  if (master->attack_finished > game->time) return;
  master->attack_finished = game->time + 2;
  if (master->message[0] != '\0') {
    qa_q1_message(game, &other, master->message, true, NULL, 0);
    qa_owned_actor player;
    if (qa_session_actor_registry_resolve_owned(game->host.actors, other, &player))
      qa_q1_sound(game, player.id, "misc/talk.wav", "voice", 1, 1);
  }
  bool silver = (qa_js_to_int32(entity->spawnflags) & 16) != 0, gold = (qa_js_to_int32(entity->spawnflags) & 8) != 0;
  if (!silver && !gold) return;
  if ((silver && qa_shared_inventory_table_count(game->host.inventory, other, "q1:key/silver") == 0) ||
      (gold && qa_shared_inventory_table_count(game->host.inventory, other, "q1:key/gold") == 0)) {
    qa_q1_sound(game, entity->actor.id,
                game->world_type == 2 ? "doors/basetry.wav"
                : game->world_type == 1 ? "doors/runetry.wav" : "doors/medtry.wav",
                "voice", 1, 1);
    if (silver != gold) {
      char message[64];
      (void)snprintf(message, sizeof message, "$qc_need_%s_%s", silver ? "silver" : "gold",
                     game->world_type == 2 ? "keycard" : game->world_type == 1 ? "runekey" : "key");
      qa_q1_message(game, &other, message, true, NULL, 0);
    }
    return;
  }
  qa_q1ad_horde_change_keys(horde, silver ? "silver" : "gold", -1);
  if (master->door_group_count == 0) master->touch = (qa_q1_bound_touch){0};
  else
    for (size_t i = 0; i < master->door_group_count; i++) master->door_group[i]->touch = (qa_q1_bound_touch){0};
  qa_q1_call_use(game, master, qa_q1_callback_registry_use(game->named, master, "door_use"), &other, &other);
}

static void spawn_door_horde(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* the handler reads no horde state */
  qa_q1_spawn_door(game, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "mg1:horde:door_touch");
}

static void found_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  qa_q1ad_horde *horde = ctx;
  qa_q1_base_monster *controller = qa_q1am_ordinary_addon_monster(horde->context, entity);
  if (controller != NULL) {
    const qa_actor_id *enemy = qa_q1_base_monster_enemy(controller);
    if (enemy == NULL) qa_q1_base_monster_play(controller, controller->spec->stand);
    else qa_q1_base_monster_found(controller, *enemy);
    return;
  }
  qa_range_error("Horde monster lost its source controller");
}

static void horde_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_context *context = horde->context;
  if (qa_q1_actor_number(entity, "horde.countedDeath") != 0) return;
  qa_q1ad_set_number(context, entity, "horde.countedDeath", 1);
  if (attacker != NULL && qa_q1_is_player(game, attacker)) {
    double spree = qa_q1ad_player_number(context, *attacker, "killspree") + 1;
    qa_q1ad_set_player_number(context, *attacker, "killspree", spree);
    qa_q1ad_set_player_number(context, *attacker, "killtime", game->time + 2);
    if (spree > 1) {
      if (spree >= 14) {
        qa_q1_msg_arg args[] = {{.is_number = true, .number = spree}};
        qa_q1_message(game, attacker, "$qc_horde_streak_generic", false, args, 1);
      } else {
        char number[32], key[64];
        qa_net_js_number(spree, number, sizeof number);
        (void)snprintf(key, sizeof key, "$qc_horde_streak_%s", number);
        qa_q1_message(game, attacker, key, false, NULL, 0);
      }
    }
  }
  qa_q1ad_spawn_horde_powerup(horde, entity);
  if (attacker != NULL && qa_q1_is_player(game, attacker)) horde->services.add_score(horde->services.ctx, *attacker, 1);
  /* The base controller owns the kill counter. Horde zombies are excluded by its source hook. */
  qa_q1_call_die(game, entity, qa_q1_callback_registry_die(game->named, entity, qa_q1_actor_text(entity, "horde.sourceDie")),
                 attacker);
  if (qa_q1_live(game, entity) && strcmp(entity->movement, "bounce") == 0 && strncmp(entity->model, "progs/h_", 8) == 0)
    qa_q1ad_horde_schedule(horde, entity, "head_fade", 1);
  else if (qa_q1_live(game, entity))
    qa_q1ad_set_number(context, entity, "horde.fadeAt", fr(game->time + 1 + 10 + qa_q1_host_random(game) * 5));
  if (!is_class(entity, "monster_zombie")) qa_q1ad_horde_remote_wavecheck(horde);
}

static void spawn_manager(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_context *context = horde->context;
  if (context->services.cvar(context->services.ctx, "horde") == 0 && game->options.deathmatch == 0)
    context->services.set_cvar(context->services.ctx, "horde", "1");
  if (entity->target[0] == '\0') qa_q1ad_set_text(&entity->target, "horde_event");
  qa_q1ad_set_text(&entity->targetname, "horde_manager");
  entity->wait = 1;
  entity->delay = 9;
  qa_q1ad_set_number(context, entity, "wave", 0);
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  qa_q1ad_set_number(context, entity, "horde.startingFlags", campaign.read_flags(campaign.ctx));
  entity->use = qa_q1_callback_registry_use(game->named, entity, "mg1:horde:check");
  qa_q1ad_horde_schedule(horde, entity, "set", 10);
}

static void spawn_monster_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* the handler reads no horde state */
  double width = is_class(entity, "info_monster_start_ranged") || is_class(entity, "info_monster_start_boss") ? 44 : 80;
  entity->wait = 0;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "mg1:horde:point");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-width, -width, 0}, {width, width, 128}});
}

static void player_attach(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game; /* extension signature */
  qa_q1ad_horde_restore_keys(ctx, player->actor.id);
}

static void player_after_physics(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds) {
  (void)seconds; /* TS afterPhysics ignores seconds */
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_context *context = horde->context;
  qa_actor_id actor = player->actor.id;
  double spree = qa_q1ad_player_number(context, actor, "killspree");
  if (spree > 0 && game->time > qa_q1ad_player_number(context, actor, "killtime")) {
    if (spree > 1) {
      double score = ceil(spree * spree / 2);
      qa_q1_msg_arg args[] = {{.is_number = true, .number = score}};
      qa_q1_message(game, &actor, "$qc_horde_streak_ended", false, args, 1);
      horde->services.add_score(horde->services.ctx, actor, score);
    }
    qa_q1ad_set_player_number(context, actor, "killspree", 0);
  }
  fade_corpses(horde);
}

static void register_action(qa_q1_services *game, qa_q1ad_horde *horde, const char *name, qa_q1_action_fn action) {
  qa_q1_callback_registry_register(game->named, name, (qa_q1_callback_handlers){.ctx = horde, .action = action});
}

qa_q1ad_horde *qa_q1ad_register_horde(qa_q1ad_context *context, qa_q1ad_horde_services services) {
  if (context->program != QA_Q1AD_MG1 && context->program != QA_Q1AD_DOPA)
    qa_range_error("Official horde requires the MG1 source program");
  qa_q1ad_horde *horde = calloc(1, sizeof *horde);
  if (horde == NULL) return NULL;
  horde->context = context;
  horde->services = services;
  horde->last_corpse_scan = -1;
  qa_q1_services *game = context->game;
  qa_q1_base_register_kill_count_rule(context->base, "mg1:horde", kill_count_rule, horde);
  horde->weapon_rules = (qa_q1_weapon_rules){.ctx = horde, .id = "mg1:horde", .attack_delay = attack_delay};
  qa_q1_register_weapon_rules(game, &horde->weapon_rules);
  register_action(game, horde, "mg1:horde:head_fade", head_fade);
  register_action(game, horde, "mg1:horde:head_fade_step", head_fade_step);
  register_action(game, horde, "mg1:horde:set", horde_set);
  register_action(game, horde, "mg1:horde:countdown", countdown);
  register_action(game, horde, "mg1:horde:countdown2", countdown2);
  register_action(game, horde, "mg1:horde:countdown3", countdown3);
  register_action(game, horde, "mg1:horde:fight", fight);
  register_action(game, horde, "mg1:horde:wave", wave_action);
  qa_q1_callback_registry_register(game->named, "mg1:horde:check",
                                   (qa_q1_callback_handlers){.ctx = horde, .action = check_action, .use = check_use});
  qa_q1_callback_registry_register(game->named, "mg1:horde:point",
                                   (qa_q1_callback_handlers){.ctx = horde, .use = point_use});
  qa_q1_callback_registry_register(game->named, "mg1:horde:door_touch",
                                   (qa_q1_callback_handlers){.ctx = horde, .touch = door_touch});
  qa_q1_register_spawn(game, "func_door", spawn_door_horde, horde);
  register_action(game, horde, "mg1:horde:found", found_action);
  qa_q1_callback_registry_register(game->named, "mg1:horde:die",
                                   (qa_q1_callback_handlers){.ctx = horde, .die = horde_die});
  qa_q1_register_spawn(game, "horde_manager", spawn_manager, horde);
  static const char *const STARTS[] = {"info_monster_start", "info_monster_start_flying",
                                       "info_monster_start_ranged", "info_monster_start_boss"};
  for (size_t i = 0; i < sizeof STARTS / sizeof STARTS[0]; i++)
    qa_q1_register_spawn(game, STARTS[i], spawn_monster_start, horde);
  horde->player_extension = (qa_q1_player_extension){
      .ctx = horde, .id = "mg1:horde", .attach = player_attach, .after_physics = player_after_physics};
  qa_q1_register_player_extension(game, &horde->player_extension);
  qa_q1ad_register_horde_loot(horde);
  return horde;
}

void qa_q1ad_horde_destroy(qa_q1ad_horde *horde) { free(horde); }
