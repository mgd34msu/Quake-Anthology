/* Port of src/content/q1/addons/horde/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/horde/index.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/horde/loot.h"
#include "qa/content/q1/addons/horde/squads.h"
#include "qa/foundation/support.h"

static qa_q1ad_horde *horde_of(qa_q1ad_context *context) { return context->slice; }
static double fr(double value) { return (double)(float)value; }

qa_q1ad_actor *qa_q1ad_horde_manager(qa_q1ad_horde *horde) {
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(horde->context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "horde_manager") == 0)
      return entities[i];
  return NULL;
}
void qa_q1ad_horde_schedule(qa_q1ad_horde *horde, qa_q1ad_actor *entity, const char *name, double delay) {
  char full[QA_Q1AD_NAME];
  (void)snprintf(full, sizeof full, "mg1:horde:%s", name);
  qa_q1ad_schedule(horde->context, entity, delay, full);
}
size_t qa_q1ad_horde_living_players(qa_q1ad_horde *horde, qa_actor_id *out, size_t capacity) {
  qa_actor_id players[64];
  size_t count = qa_q1ad_players(horde->context, players, 64), written = 0;
  for (size_t i = 0; i < count; i++)
    if (qa_q1ad_health(horde->context, players[i]) > 0 && written < capacity) out[written++] = players[i];
  return written;
}
size_t qa_q1ad_horde_living_monsters(qa_q1ad_horde *horde, qa_q1ad_actor **out, size_t capacity) {
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(horde->context, entities, 512), written = 0;
  for (size_t i = 0; i < count; i++) {
    const char *classname = entities[i]->classname != NULL ? entities[i]->classname : "";
    if (strcmp(qa_q1ad_actor_text(entities[i], "category"), "monster") == 0 &&
        strcmp(classname, "monster_zombie") != 0 &&
        qa_q1ad_health(horde->context, entities[i]->actor.id) > 0 && written < capacity)
      out[written++] = entities[i];
  }
  return written;
}
bool qa_q1ad_horde_target(qa_q1ad_horde *horde, qa_actor_id *out) {
  qa_actor_id living[64];
  size_t count = qa_q1ad_horde_living_players(horde, living, 64);
  if (count == 0) return false;
  double roll = qa_q1ad_random(horde->context) * (double)count;
  double index = 0;
  qa_actor_id players[64];
  size_t player_count = qa_q1ad_players(horde->context, players, 64);
  for (size_t i = 0; i < player_count; i++) {
    if (qa_q1ad_health(horde->context, players[i]) > 0) index++;
    bool no_target = horde->services.no_target != NULL &&
        horde->services.no_target(horde->services.ctx, players[i]);
    if (roll <= index && !no_target) { *out = players[i]; return true; }
  }
  return false;
}
bool qa_q1ad_horde_blocked(qa_q1ad_horde *horde, qa_q1ad_actor *point) {
  if (((int32_t)point->spawnflags & 2) != 0) return false;
  qa_q1ad_body source = qa_q1ad_body_of(horde->context, point);
  qa_vec3 min = qa_add3(source.origin, source.bounds.min);
  qa_vec3 max = qa_add3(source.origin, source.bounds.max);
  qa_actor_id living[64];
  size_t count = qa_q1ad_horde_living_players(horde, living, 64);
  for (size_t i = 0; i < count; i++) {
    if (horde->services.dead_flag != NULL &&
        horde->services.dead_flag(horde->services.ctx, living[i]) > 0) continue;
    qa_q1ad_body body;
    if (!qa_q1ad_body_by_id(horde->context, living[i], &body)) continue;
    qa_vec3 low = qa_add3(body.origin, body.bounds.min);
    qa_vec3 high = qa_add3(body.origin, body.bounds.max);
    /* Official CheckBlockedSpawn repeats the Y comparison and does not test Z. */
    if (high.x > min.x && low.x < max.x && high.y > min.y && low.y < max.y) return true;
  }
  return false;
}
static size_t spawn_points(qa_q1ad_horde *horde, const char *type, qa_q1ad_actor **out, size_t capacity) {
  char classname[64];
  if (strcmp(type, "normal") == 0) (void)snprintf(classname, sizeof classname, "info_monster_start");
  else (void)snprintf(classname, sizeof classname, "info_monster_start_%s", type);
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(horde->context, entities, 512), written = 0;
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, classname) == 0 && written < capacity)
      out[written++] = entities[i];
  return written;
}
static bool spawn_valid(qa_q1ad_horde *horde, qa_q1ad_actor *point) {
  return horde->context->game.time > point->wait &&
      ((int32_t)point->spawnflags & 1) == 0 && !qa_q1ad_horde_blocked(horde, point);
}
qa_q1ad_actor *qa_q1ad_horde_find_spawn(qa_q1ad_horde *horde, const char *type) {
  qa_q1ad_actor *candidates[128];
  size_t count = spawn_points(horde, type, candidates, 128);
  bool any = false;
  for (size_t i = 0; i < count; i++) if (spawn_valid(horde, candidates[i])) any = true;
  if (!any) {
    if (strcmp(type, "normal") == 0) return NULL;
    count = spawn_points(horde, "normal", candidates, 128);
    any = false;
    for (size_t i = 0; i < count; i++) if (spawn_valid(horde, candidates[i])) any = true;
  }
  if (!any) return NULL;
  double roll = (double)count * qa_q1ad_random(horde->context);
  for (size_t i = 0; i < count; i++)
    if ((double)i + 1 >= roll && spawn_valid(horde, candidates[i])) return candidates[i];
  for (size_t i = 0; i < count; i++) if (spawn_valid(horde, candidates[i])) return candidates[i];
  return NULL;
}
static double context_random(void *opaque) { return qa_q1ad_random(opaque); }

qa_q1ad_actor *qa_q1ad_horde_spawn_monster(qa_q1ad_horde *horde, const char *kind, qa_vec3 origin,
                                           qa_vec3 angles, qa_q1ad_actor *owner) {
  qa_q1ad_context *context = horde->context;
  char classname[64];
  if (strcmp(kind, "grunt") == 0) (void)snprintf(classname, sizeof classname, "monster_army");
  else if (strcmp(kind, "hellknight") == 0) (void)snprintf(classname, sizeof classname, "monster_hell_knight");
  else if (strcmp(kind, "demon") == 0) (void)snprintf(classname, sizeof classname, "monster_demon1");
  else (void)snprintf(classname, sizeof classname, "monster_%s", kind);
  if (context->game.create == NULL) return NULL;
  qa_q1ad_actor *entity = context->game.create(context->game.ctx, classname);
  if (entity == NULL) return NULL;
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = origin, .has_angles = true, .angles = angles};
  qa_q1ad_set_body(context, entity, &patch);
  if (context->game.spawn_entity != NULL) context->game.spawn_entity(context->game.ctx, entity);
  qa_q1ad_monster_info controller = {0};
  bool has_controller = context->game.monster_controller != NULL &&
      context->game.monster_controller(context->game.ctx, entity, &controller) && controller.present;
  const char *species = entity->monster_species;
  if (!has_controller && (species == NULL ||
      (strcmp(species, "army") != 0 && strcmp(species, "dog") != 0)))
    qa_range_error("Horde has no native monster controller for %s", kind);
  qa_q1ad_cancel(context, entity);
  if (has_controller) {
    char model[80];
    (void)snprintf(model, sizeof model, "progs/%s.mdl", controller.model != NULL ? controller.model : "");
    qa_q1ad_actor_set(entity, "horde.model", model);
    entity->model = qa_q1ad_actor_text(entity, "horde.model");
    entity->solid = "slidebox";
    entity->movement = "step";
    entity->aimed_damage = true;
    qa_q1ad_set_bounds(context, entity, controller.bounds);
    qa_q1ad_set_vector(context, entity, "view_ofs", (qa_vec3){0, 0, 25});
    if (context->game.combat_set_team_q1monsters != NULL)
      context->game.combat_set_team_q1monsters(context->game.ctx, entity->actor);
    if (strcmp(kind, "wizard") == 0) entity->movement_flags |= 1;
  }
  double offset = strcmp(kind, "demon") == 0 ? 48
      : strcmp(kind, "ogre") == 0 || strcmp(kind, "shambler") == 0 || strcmp(kind, "shalrath") == 0 ||
        strcmp(kind, "wizard") == 0 || strcmp(kind, "zombie") == 0 ? 32 : 24;
  qa_vec3 position = qa_add3(origin, (qa_vec3){0, 0, offset + 1});
  if (strcmp(kind, "wizard") != 0 && context->game.trace != NULL) {
    qa_actor_id self = entity->actor.id;
    qa_q1ad_trace trace = {.fraction = 1, .end = qa_sub3(position, (qa_vec3){0, 0, 256})};
    (void)context->game.trace(context->game.ctx, position, trace.end,
        qa_q1ad_body_of(context, entity).bounds, &self, true, &trace);
    if (trace.fraction < 1 && !trace.all_solid) {
      position = trace.end;
      qa_q1ad_body_patch ground = {.has_ground = true, .ground = trace.actor, .ground_null = !trace.has_actor};
      qa_q1ad_set_body(context, entity, &ground);
      entity->movement_flags |= 512;
    }
  }
  qa_q1ad_set_origin(context, entity, position);
  if (context->game.walk_move != NULL) context->game.walk_move(context->game.ctx, entity->actor, 0, 0);
  qa_q1ad_actor_set(entity, "category", "monster");
  entity->damageable = true;
  entity->owner = owner->actor.id;
  entity->has_owner = true;
  entity->yaw_speed = 20;
  entity->movement_flags |= 32;
  qa_actor_id target;
  bool has_target = qa_q1ad_horde_target(horde, &target);
  if (has_controller) {
    if (context->game.monster_set_enemy != NULL)
      context->game.monster_set_enemy(context->game.ctx, entity, has_target ? &target : NULL);
  } else if (entity->has_monster) {
    entity->monster_has_enemy = has_target;
    if (has_target) entity->monster_enemy = target;
  }
  if (entity->die[0] == '\0') qa_range_error("Horde monster has no named source death");
  qa_q1ad_actor_set(entity, "horde.sourceDie", entity->die);
  qa_q1ad_bind(entity->die, "mg1:horde:die");
  if (strcmp(kind, "zombie") == 0) context->game.total_monsters--;
  qa_q1ad_horde_schedule(horde, entity, "found", 0.1);
  qa_q1ad_actor *death = context->game.create != NULL ? context->game.create(context->game.ctx, "teledeath") : NULL;
  if (death != NULL) {
    qa_bounds bounds = qa_q1ad_body_of(context, entity).bounds;
    death->owner = entity->actor.id;
    death->has_owner = true;
    death->solid = "trigger";
    qa_q1ad_bind(death->touch, "tdeath_touch");
    qa_bounds death_bounds = {qa_sub3(bounds.min, (qa_vec3){1, 1, 1}), qa_add3(bounds.max, (qa_vec3){1, 1, 1})};
    qa_q1ad_body_patch death_patch = {.has_origin = true, .origin = position,
                                      .has_bounds = true, .bounds = death_bounds};
    qa_q1ad_set_body(context, death, &death_patch);
    qa_q1ad_link(context, death);
    qa_bounds box = qa_q1ad_body_of(context, death).bounds;
    qa_actor_id observed[256];
    size_t observed_count = context->game.observations != NULL
        ? context->game.observations(context->game.ctx, observed, 256) : 0;
    for (size_t i = 0; i < observed_count; i++) {
      qa_q1ad_body state;
      if (!qa_q1ad_body_by_id(context, observed[i], &state)) continue;
      qa_bounds mine = {qa_add3(position, box.min), qa_add3(position, box.max)};
      qa_bounds theirs = {qa_add3(state.origin, state.bounds.min), qa_add3(state.origin, state.bounds.max)};
      if (qa_math_bounds_overlap(mine, theirs) && context->game.host_touch != NULL)
        context->game.host_touch(context->game.ctx, death->actor, observed[i]);
    }
    qa_q1ad_schedule(context, death, 0.01, "SUB_Remove");
  }
  return entity;
}

bool qa_q1ad_horde_prepare(qa_q1ad_horde *horde, qa_q1ad_actor *manager) {
  qa_q1ad_context *context = horde->context;
  qa_q1ad_set_number(context, manager, "key_spawned", 0);
  qa_actor_id living[64];
  double players = (double)qa_q1ad_horde_living_players(horde, living, 64);
  if (players < 1) return false;
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_horde_item") == 0 &&
        entities[i]->wait == 0)
      qa_q1ad_horde_schedule(horde, entities[i], "item", qa_q1ad_random(context) * 2);
  double wave = qa_q1ad_actor_number(manager, "wave") + 1;
  double skill = context->game.skill;
  double level = wave + (skill >= 3 ? 6 : skill >= 2 ? 3 : 0);
  bool army = fmod(level + 2, 3) == 0;
  qa_q1ad_set_number(context, manager, "wave", wave);
  qa_q1ad_set_number(context, manager, "army", army ? 1 : 0);
  double scale = players >= 4 ? 2 : players >= 3 ? 1.5 : players >= 2 ? 1.25 : 1;
  double bosses = qa_q1ad_actor_number(manager, "bosses");
  if (fmod(level, 3) == 0) bosses = floor((level + 1) / 4);
  else if (skill > 1 && !army && level > 9) bosses = floor((level + 1) / 8);
  double elites = floor((ceil((level - 1) / 3) - floor(bosses / 2)) * scale);
  double fodder = floor((level + 2 - (bosses * 2 + elites)) * scale);
  qa_q1ad_set_number(context, manager, "bosses", bosses);
  qa_q1ad_set_number(context, manager, "elites", elites);
  qa_q1ad_set_number(context, manager, "fodder", fodder);
  qa_q1ad_actor *found[64];
  size_t found_count = qa_q1ad_find(context, manager->target != NULL ? manager->target : "", found, 64);
  for (size_t i = 0; i < found_count; i++)
    if (found[i]->use[0] != '\0')
      qa_q1ad_do_use(context, found[i], found[i]->use, &manager->actor.id,
                     manager->has_activator ? &manager->activator : NULL);
  manager->wait = 1;
  return true;
}

void qa_q1ad_horde_spawn_wave(qa_q1ad_horde *horde, qa_q1ad_actor *manager) {
  qa_q1ad_context *context = horde->context;
  const char *category = qa_q1ad_actor_number(manager, "fodder") > 0 ? "fodder"
      : qa_q1ad_actor_number(manager, "elites") > 0 ? "elites" : "bosses";
  const char *squad = NULL, *type = NULL;
  if (!qa_q1ad_choose_horde_squad(qa_q1ad_actor_number(manager, "army") != 0, category,
                                  context_random, context, &squad, &type)) {
    qa_q1ad_set_number(context, manager, "bosses", 0);
    qa_q1ad_horde_schedule(horde, manager, "wave", 1);
    return;
  }
  qa_q1ad_actor *point = qa_q1ad_horde_find_spawn(horde, type);
  if (point == NULL) { qa_q1ad_horde_schedule(horde, manager, "wave", 1); return; }
  point->wait = context->game.time + 5;
  qa_q1ad_use_targets(context, point, manager->has_activator ? &manager->activator : NULL);
  qa_q1ad_body body = qa_q1ad_body_of(context, point);
  qa_q1ad_squad_spawn spawns[8];
  size_t spawn_count = qa_q1ad_horde_squad(squad, context->game.skill, context_random, context, spawns, 8);
  for (size_t i = 0; i < spawn_count; i++)
    (void)qa_q1ad_horde_spawn_monster(horde, spawns[i].monster,
        qa_add3(body.origin, spawns[i].offset), body.angles, manager);
  qa_q1ad_effect(context, "teleport", body.origin, NULL, 0);
  qa_q1ad_set_number(context, manager, category, qa_q1ad_actor_number(manager, category) - 1);
  if (qa_q1ad_actor_number(manager, "fodder") + qa_q1ad_actor_number(manager, "elites") +
      qa_q1ad_actor_number(manager, "bosses") <= 0) {
    manager->wait = 0;
    qa_q1ad_horde_schedule(horde, manager, "check", 30);
    return;
  }
  qa_q1ad_horde_schedule(horde, manager, "wave", 2 + qa_q1ad_random(context));
}

void qa_q1ad_horde_check_wave(qa_q1ad_horde *horde, qa_q1ad_actor *manager) {
  qa_q1ad_context *context = horde->context;
  if (manager->wait != 0) return;
  qa_q1ad_horde_schedule(horde, manager, "check", 10);
  if (context->game.killed_monsters + 3 >= context->game.total_monsters) {
    qa_q1ad_actor *entities[512];
    size_t count = qa_q1ad_entities(context, entities, 512);
    for (size_t i = 0; i < count; i++)
      if (strcmp(qa_q1ad_actor_text(entities[i], "category"), "monster") == 0 &&
          qa_q1ad_health(context, entities[i]->actor.id) <= 0)
        qa_q1ad_actor_set(entities[i], "category", "");
  }
  double wave = qa_q1ad_actor_number(manager, "wave");
  qa_q1ad_actor *monsters[512];
  size_t living = qa_q1ad_horde_living_monsters(horde, monsters, 512);
  if ((double)living > (fmod(wave, 3) == 0 || wave < 3 ? 0 : 5)) return;
  qa_actor_id players[64];
  size_t player_count = qa_q1ad_players(context, players, 64);
  for (size_t i = 0; i < player_count; i++)
    if (horde->services.dead_flag != NULL &&
        horde->services.dead_flag(horde->services.ctx, players[i]) > 0 &&
        horde->services.respawn_teammate != NULL)
      horde->services.respawn_teammate(horde->services.ctx, players[i]);
  manager->wait = 1;
  if (fmod(wave, 3) == 0) {
    if (qa_q1ad_actor_number(manager, "key_spawned") == 0) qa_q1ad_horde_get_key(horde, manager);
    qa_q1ad_horde_schedule(horde, manager, "countdown", 20);
    return;
  }
  qa_q1ad_horde_schedule(horde, manager, "countdown", 0);
}
void qa_q1ad_horde_remote_wavecheck(qa_q1ad_horde *horde) {
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL || horde->context->game.intermission) return;
  qa_q1ad_horde_check_wave(horde, manager);
}
void qa_q1ad_horde_get_key(qa_q1ad_horde *horde, qa_q1ad_actor *manager) {
  qa_q1ad_actor *entities[512], *points[64];
  size_t count = qa_q1ad_entities(horde->context, entities, 512), point_count = 0;
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_horde_key") == 0 &&
        point_count < 64) points[point_count++] = entities[i];
  if (point_count == 0) { qa_q1ad_horde_schedule(horde, manager, "countdown", 4); return; }
  double wave = qa_q1ad_actor_number(manager, "wave");
  int64_t flag = wave <= 3 ? 1 : wave <= 6 ? 2 : wave <= 9 ? 4 : 8;
  qa_q1ad_actor *point = NULL;
  for (size_t i = 0; i < point_count; i++)
    if (((int32_t)points[i]->spawnflags & flag) != 0) { point = points[i]; break; }
  const char *which = point == NULL ? (wave == 9 ? "gold" : "silver") : flag == 4 ? "gold" : "silver";
  qa_q1ad_horde_schedule(horde, point != NULL ? point : points[0], which, 0);
}
void qa_q1ad_horde_restore_keys(qa_q1ad_horde *horde, qa_actor_id player) {
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  qa_owned_actor actor;
  if (manager == NULL || !qa_q1ad_resolve_owned(horde->context, player, &actor)) return;
  const char *keys[2] = {"silver", "gold"};
  for (int i = 0; i < 2; i++) {
    char field[32], item[32];
    (void)snprintf(field, sizeof field, "keys_%s", keys[i]);
    (void)snprintf(item, sizeof item, "q1:key/%s", keys[i]);
    if (qa_q1ad_actor_number(manager, field) > 0 && horde->context->game.inventory_give != NULL)
      horde->context->game.inventory_give(horde->context->game.ctx, actor, item, 1);
  }
}
void qa_q1ad_horde_change_keys(qa_q1ad_horde *horde, const char *key, int delta) {
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL) qa_range_error("Horde keys require their manager");
  char field[32], item[32];
  (void)snprintf(field, sizeof field, "keys_%s", key);
  (void)snprintf(item, sizeof item, "q1:key/%s", key);
  double count = qa_q1ad_actor_number(manager, field) + delta;
  qa_q1ad_set_number(horde->context, manager, field, count);
  if ((delta == 1 && count == 1) || (delta == -1 && count == 0)) {
    qa_actor_id players[64];
    size_t player_count = qa_q1ad_players(horde->context, players, 64);
    for (size_t i = 0; i < player_count; i++) {
      qa_owned_actor actor;
      if (!qa_q1ad_resolve_owned(horde->context, players[i], &actor)) continue;
      if (delta == 1) {
        if (horde->context->game.inventory_give != NULL)
          horde->context->game.inventory_give(horde->context->game.ctx, actor, item, 1);
      } else if (horde->context->game.inventory_consume != NULL)
        (void)horde->context->game.inventory_consume(horde->context->game.ctx, actor, item, 1);
    }
  }
}
void qa_q1ad_horde_teammate_killed(qa_q1ad_horde *horde, qa_actor_id attacker) {
  if (qa_q1ad_cvar(horde->context, "horde") == 0) return;
  qa_q1ad_set_player_number(horde->context, attacker, "killtime", 0);
  if (horde->services.add_score != NULL) horde->services.add_score(horde->services.ctx, attacker, -2);
}
bool qa_q1ad_horde_request_respawn(qa_q1ad_horde *horde) {
  if (qa_q1ad_horde_manager(horde) == NULL) return false;
  qa_actor_id living[64];
  if (!horde->context->game.coop || qa_q1ad_horde_living_players(horde, living, 64) == 0)
    qa_q1ad_horde_restart_after_defeat(horde);
  return true;
}
void qa_q1ad_horde_restart_after_defeat(qa_q1ad_horde *horde) {
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL) qa_range_error("Horde restart requires its manager");
  double flags = qa_q1ad_actor_number(manager, "horde.startingFlags");
  if (horde->context->game.campaign_write_flags != NULL)
    horde->context->game.campaign_write_flags(horde->context->game.ctx, flags);
  if (horde->services.restart_session != NULL)
    horde->services.restart_session(horde->services.ctx,
        horde->context->game.map_name != NULL ? horde->context->game.map_name : "", flags);
}
bool qa_q1ad_horde_kill_count_rule(qa_q1ad_horde *horde, qa_q1ad_actor *monster) {
  const char *classname = monster->classname != NULL ? monster->classname : "";
  return qa_q1ad_horde_manager(horde) == NULL || strcmp(classname, "monster_zombie") != 0;
}
double qa_q1ad_horde_attack_delay(qa_q1ad_horde *horde, qa_q1ad_player *player, double delay) {
  qa_q1ad_context *context = horde->context;
  double flags = context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
  if (player->weapon == NULL || strcmp(player->weapon, "axe") != 0 ||
      qa_q1ad_cvar(context, "horde") == 0 || ((int32_t)flags & 4) == 0) return delay;
  double chain = qa_q1ad_player_number(context, player->actor.id, "axe_hit_chain");
  bool chop = chain >= 2 && context->game.time <
      qa_q1ad_player_number(context, player->actor.id, "axe_hit_chain_time");
  player->weapon_animation_base = chop ? 1 : 5;
  return chop ? 0.8 : chain > 1 ? 0.6 : 0.4;
}
void qa_q1ad_horde_player_attach(qa_q1ad_horde *horde, qa_q1ad_player *player) {
  qa_q1ad_horde_restore_keys(horde, player->actor.id);
}
void qa_q1ad_horde_after_physics(qa_q1ad_horde *horde, qa_q1ad_player *player) {
  qa_q1ad_context *context = horde->context;
  double spree = qa_q1ad_player_number(context, player->actor.id, "killspree");
  if (spree > 0 && context->game.time > qa_q1ad_player_number(context, player->actor.id, "killtime")) {
    if (spree > 1) {
      double score = ceil(spree * spree / 2);
      qa_q1ad_msg_arg args[] = {{.is_number = true, .number = score}};
      qa_q1ad_message(context, player->actor.id, "$qc_horde_streak_ended", false, args, 1);
      if (horde->services.add_score != NULL) horde->services.add_score(horde->services.ctx, player->actor.id, score);
    }
    qa_q1ad_set_player_number(context, player->actor.id, "killspree", 0);
  }
}

/* --- registered handlers (Q1Horde constructor) --- */
static void sound_talk(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_sound_entity(context, entity, "misc/talk.wav", "voice");
}
static void head_fade(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_cvar(context, "horde") == 0) { qa_q1ad_cancel(context, entity); return; }
  if (qa_q1ad_actor_number(entity, "alpha") == 0) qa_q1ad_alpha(context, entity, 1);
  qa_q1ad_horde_schedule(horde_of(context), entity, "head_fade_step", 10 + qa_q1ad_random(context) * 5);
}
static void head_fade_step(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_number(entity, "alpha") <= 0) { qa_q1ad_remove(context, entity); return; }
  qa_q1ad_alpha(context, entity, fr(qa_q1ad_actor_number(entity, "alpha") - context->frame_time));
  qa_q1ad_horde_schedule(horde_of(context), entity, "head_fade_step", 0);
}
static void horde_set(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_horde_schedule(horde_of(context), entity, "countdown", 1);
}
static void countdown(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (!qa_q1ad_horde_prepare(horde_of(context), entity)) return;
  qa_q1ad_broadcast(context, "3");
  sound_talk(context, entity);
  qa_q1ad_horde_schedule(horde_of(context), entity, "countdown2", 1);
}
static void countdown2(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_broadcast(context, "2");
  sound_talk(context, entity);
  qa_q1ad_horde_schedule(horde_of(context), entity, "countdown3", 1);
}
static void countdown3(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_broadcast(context, "1");
  sound_talk(context, entity);
  qa_q1ad_horde_schedule(horde_of(context), entity, "fight", 1);
}
static void fight(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_broadcast(context, fmod(qa_q1ad_actor_number(entity, "wave"), 3) == 0
      ? "$qc_horde_boss_wave" : "$qc_horde_fight");
  qa_q1ad_horde_spawn_wave(horde_of(context), entity);
  sound_talk(context, entity);
}
static void wave_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_horde_spawn_wave(horde_of(context), entity);
}
static void check_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_horde_check_wave(horde_of(context), entity);
}
static void check_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_horde_check_wave(horde_of(context), entity);
}
static void point_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)context; (void)other; (void)activator;
  entity->spawnflags = (double)((int32_t)entity->spawnflags ^ 1);
}
static void door_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  qa_q1ad_horde *horde = horde_of(context);
  if (qa_q1ad_horde_manager(horde) == NULL) {
    qa_q1ad_do_touch(context, entity, "door_touch", other, normal);
    return;
  }
  if (!qa_q1ad_is_player(context, other)) return;
  qa_q1ad_actor *master = entity->door_group_count > 0 ? entity->door_group[0] : entity;
  if (master->attack_finished > context->game.time) return;
  master->attack_finished = context->game.time + 2;
  if (master->message != NULL && master->message[0] != '\0') {
    qa_q1ad_message(context, *other, master->message, false, NULL, 0);
    qa_owned_actor player;
    if (qa_q1ad_resolve_owned(context, *other, &player))
      qa_q1ad_sound_actor(context, player.id, "misc/talk.wav", "voice");
  }
  bool silver = ((int32_t)entity->spawnflags & 16) != 0;
  bool gold = ((int32_t)entity->spawnflags & 8) != 0;
  if (!silver && !gold) return;
  double silver_count = context->game.inventory_count != NULL
      ? context->game.inventory_count(context->game.ctx, *other, "q1:key/silver") : 0;
  double gold_count = context->game.inventory_count != NULL
      ? context->game.inventory_count(context->game.ctx, *other, "q1:key/gold") : 0;
  if ((silver && silver_count == 0) || (gold && gold_count == 0)) {
    qa_q1ad_sound_entity(context, entity, context->game.world_type == 2 ? "doors/basetry.wav"
        : context->game.world_type == 1 ? "doors/runetry.wav" : "doors/medtry.wav", "voice");
    if (silver != gold) {
      char message[64];
      (void)snprintf(message, sizeof message, "$qc_need_%s_%s", silver ? "silver" : "gold",
          context->game.world_type == 2 ? "keycard" : context->game.world_type == 1 ? "runekey" : "key");
      qa_q1ad_message(context, *other, message, false, NULL, 0);
    }
    return;
  }
  qa_q1ad_horde_change_keys(horde, silver ? "silver" : "gold", -1);
  if (master->door_group_count == 0) master->touch[0] = '\0';
  else for (size_t i = 0; i < master->door_group_count; i++) master->door_group[i]->touch[0] = '\0';
  qa_q1ad_do_use(context, master, "door_use", other, other);
}
static void spawn_door_horde(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.spawn_door != NULL) context->game.spawn_door(context->game.ctx, entity);
  qa_q1ad_bind(entity->touch, "mg1:horde:door_touch");
}
static void found_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_monster_info controller = {0};
  bool has_controller = context->game.monster_controller != NULL &&
      context->game.monster_controller(context->game.ctx, entity, &controller) && controller.present;
  if (has_controller) {
    if (!controller.has_enemy) {
      if (context->game.monster_play_stand != NULL) context->game.monster_play_stand(context->game.ctx, entity);
    } else if (context->game.monster_found != NULL)
      context->game.monster_found(context->game.ctx, entity, controller.enemy);
    return;
  }
  const char *species = entity->monster_species;
  if (species != NULL && (strcmp(species, "army") == 0 || strcmp(species, "dog") == 0)) {
    qa_q1ad_do_action(context, entity, "monster_found_target");
    return;
  }
  qa_range_error("Horde monster lost its source controller");
}
static void horde_die(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *attacker) {
  qa_q1ad_horde *horde = horde_of(context);
  if (qa_q1ad_actor_number(entity, "horde.countedDeath") != 0) return;
  qa_q1ad_set_number(context, entity, "horde.countedDeath", 1);
  if (attacker != NULL && qa_q1ad_is_player(context, attacker)) {
    double spree = qa_q1ad_player_number(context, *attacker, "killspree") + 1;
    qa_q1ad_set_player_number(context, *attacker, "killspree", spree);
    qa_q1ad_set_player_number(context, *attacker, "killtime", context->game.time + 2);
    if (spree > 1) {
      if (spree >= 14) {
        qa_q1ad_msg_arg args[] = {{.is_number = true, .number = spree}};
        qa_q1ad_message(context, *attacker, "$qc_horde_streak_generic", false, args, 1);
      } else {
        char key[48];
        (void)snprintf(key, sizeof key, "$qc_horde_streak_%d", (int)spree);
        qa_q1ad_message(context, *attacker, key, false, NULL, 0);
      }
    }
  }
  qa_q1ad_spawn_horde_powerup(horde, entity);
  if (attacker != NULL && qa_q1ad_is_player(context, attacker) &&
      horde->services.add_score != NULL)
    horde->services.add_score(horde->services.ctx, *attacker, 1);
  /* The base controller owns the kill counter. Horde zombies are excluded by its source hook. */
  qa_q1ad_do_die(context, entity, qa_q1ad_actor_text(entity, "horde.sourceDie"), attacker);
  const char *model = entity->model != NULL ? entity->model : "";
  if (qa_q1ad_live(context, entity) && entity->movement != NULL &&
      strcmp(entity->movement, "bounce") == 0 && strncmp(model, "progs/h_", 8) == 0)
    qa_q1ad_horde_schedule(horde, entity, "head_fade", 1);
  const char *classname = entity->classname != NULL ? entity->classname : "";
  if (strcmp(classname, "monster_zombie") != 0) qa_q1ad_horde_remote_wavecheck(horde);
}
static void spawn_manager(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_horde *horde = horde_of(context);
  if (qa_q1ad_cvar(context, "horde") == 0 && context->game.deathmatch == 0 &&
      context->services.set_cvar != NULL)
    context->services.set_cvar(context->services.ctx, "horde", "1");
  if (entity->target == NULL || entity->target[0] == '\0') entity->target = "horde_event";
  entity->targetname = "horde_manager";
  entity->wait = 1;
  entity->delay = 9;
  qa_q1ad_set_number(context, entity, "wave", 0);
  double flags = context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
  qa_q1ad_set_number(context, entity, "horde.startingFlags", flags);
  qa_q1ad_bind(entity->use, "mg1:horde:check");
  qa_q1ad_horde_schedule(horde, entity, "set", 10);
}
static void spawn_monster_start(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const char *classname = entity->classname != NULL ? entity->classname : "";
  double width = strcmp(classname, "info_monster_start_ranged") == 0 ||
      strcmp(classname, "info_monster_start_boss") == 0 ? 44 : 80;
  entity->wait = 0;
  qa_q1ad_bind(entity->use, "mg1:horde:point");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-width, -width, 0}, {width, width, 128}});
}

void qa_q1ad_horde_init(qa_q1ad_horde *horde, qa_q1ad_context *context, qa_q1ad_horde_services services) {
  *horde = (qa_q1ad_horde){.context = context, .services = services};
  if (context->program != QA_Q1AD_MG1 && context->program != QA_Q1AD_DOPA)
    qa_range_error("Official horde requires the MG1 source program");
  context->slice = horde;
  if (context->game.register_rule != NULL) {
    context->game.register_rule(context->game.ctx, "mg1:horde:kill-count");
    context->game.register_rule(context->game.ctx, "mg1:horde");
    context->game.register_rule(context->game.ctx, "mg1:horde:player");
  }
  qa_q1ad_register_named(context, "mg1:horde:head_fade", (qa_q1ad_handlers){.action = head_fade});
  qa_q1ad_register_named(context, "mg1:horde:head_fade_step", (qa_q1ad_handlers){.action = head_fade_step});
  qa_q1ad_register_named(context, "mg1:horde:set", (qa_q1ad_handlers){.action = horde_set});
  qa_q1ad_register_named(context, "mg1:horde:countdown", (qa_q1ad_handlers){.action = countdown});
  qa_q1ad_register_named(context, "mg1:horde:countdown2", (qa_q1ad_handlers){.action = countdown2});
  qa_q1ad_register_named(context, "mg1:horde:countdown3", (qa_q1ad_handlers){.action = countdown3});
  qa_q1ad_register_named(context, "mg1:horde:fight", (qa_q1ad_handlers){.action = fight});
  qa_q1ad_register_named(context, "mg1:horde:wave", (qa_q1ad_handlers){.action = wave_action});
  qa_q1ad_register_named(context, "mg1:horde:check",
      (qa_q1ad_handlers){.action = check_action, .use = check_use});
  qa_q1ad_register_named(context, "mg1:horde:point", (qa_q1ad_handlers){.use = point_use});
  qa_q1ad_register_named(context, "mg1:horde:door_touch", (qa_q1ad_handlers){.touch = door_touch});
  qa_q1ad_register_named(context, "mg1:horde:found", (qa_q1ad_handlers){.action = found_action});
  qa_q1ad_register_named(context, "mg1:horde:die", (qa_q1ad_handlers){.die = horde_die});
  qa_q1ad_register_spawn(context, "func_door", false, spawn_door_horde);
  qa_q1ad_register_spawn(context, "horde_manager", false, spawn_manager);
  qa_q1ad_register_spawn(context, "info_monster_start", false, spawn_monster_start);
  qa_q1ad_register_spawn(context, "info_monster_start_flying", false, spawn_monster_start);
  qa_q1ad_register_spawn(context, "info_monster_start_ranged", false, spawn_monster_start);
  qa_q1ad_register_spawn(context, "info_monster_start_boss", false, spawn_monster_start);
  qa_q1ad_register_horde_loot(horde);
}
qa_q1ad_horde *qa_q1ad_register_horde(qa_q1ad_horde *horde, qa_q1ad_context *context, qa_q1ad_horde_services services) {
  qa_q1ad_horde_init(horde, context, services);
  return horde;
}

