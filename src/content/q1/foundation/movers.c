/*
 * Port of src/content/q1/foundation/movers.ts. GPL-2.0-or-later.
 * Each door owns a copy of its door-group array (TS shares one array;
 * the group is never mutated after linkDoors, and qa_q1_actor_free
 * releases door_group per actor).
 */
#include "qa/content/q1/foundation/movers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static double fr(double value) { return (float)value; }
static qa_vec3 v3(double x, double y, double z) {
  const qa_vec3 result = {x, y, z};
  return result;
}
static void set_string(char **slot, const char *value) {
  if (value == NULL) value = "";
  if (*slot != NULL && strcmp(*slot, value) == 0) return;
  const size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (!copy) return;
  memcpy(copy, value, len);
  free(*slot);
  *slot = copy;
}
/* TS `spawnflags & x` applies ToInt32: the int32 view of the number. */
static int32_t flags_of(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
static void set_activator(qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator) entity->activator = *activator;
}
static const qa_actor_id *activator_of(const qa_q1_actor *entity) {
  return entity->has_activator ? &entity->activator : NULL;
}
static void set_angles_zero(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_body patch = qa_q1_get_body(game, entity);
  patch.angles = QA_Q1_ZERO;
  qa_q1_set_body_patch(game, entity, &patch, false, true, false, false, false);
}
static void precache_sound(qa_q1_services *game, const char *path) {
  if (qa_q1_services_uses_id1(game)) (void)qa_q1_services_precache_sound(game, path);
}
static qa_q1_bound_action named_action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(game->named, entity, name);
}

/* MG1 and MG3 let a door, key or button override the map's worldtype (SUB_SetWorldtype). */
static double entity_world_type(qa_q1_services *game, const qa_q1_actor *entity) {
  const double authored = game->source_door_keys ? qa_q1_actor_number(entity, "worldtype") : 0;
  return authored > 0 && authored <= 3 ? authored - 1 : game->world_type;
}
enum { DOOR_CRUSHER = 64, DOOR_NONSOLID = 4096 };
/* MG1 and MG3 doors and buttons take a map-authored `movedir` vector as the
 * whole travel instead of deriving one from angles; false = TS null. */
static bool authored_move_direction(qa_q1_services *game, const qa_q1_actor *entity, qa_vec3 *out) {
  if (!game->source_door_keys || qa_q1_actor_field_get(entity, "movedir") == NULL) return false;
  const qa_vec3 direction = qa_q1_actor_vector(entity, "movedir");
  if (direction.x == 0 && direction.y == 0 && direction.z == 0) return false;
  *out = direction;
  return true;
}
/* TS lockKey: gold (8), silver (16), or NULL. */
static const char *lock_key(const qa_q1_actor *entity) {
  return (qa_js_to_int32(entity->spawnflags) & 8) != 0    ? "q1:key/gold"
         : (qa_js_to_int32(entity->spawnflags) & 16) != 0 ? "q1:key/silver" : NULL;
}

static const char *door_sound(const qa_q1_actor *entity, bool moving) {
  if (entity->sounds == 1) return moving ? "doors/doormv1.wav" : "doors/drclos4.wav";
  if (entity->sounds == 2) return moving ? "doors/hydro1.wav" : "doors/hydro2.wav";
  if (entity->sounds == 3) return moving ? "doors/stndr1.wav" : "doors/stndr2.wav";
  if (entity->sounds == 4) return moving ? "doors/ddoor1.wav" : "doors/ddoor2.wav";
  return "misc/null.wav";
}

void qa_q1_door_down(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_sound_defaults(game, entity->actor.id, door_sound(entity, true));
  entity->state = "down";
  if (entity->max_health > 0) {
    qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
    qa_q1_actor_set_damageable(entity, true);
  }
  double closing = entity->speed;
  if (game->source_door_keys) {
    const double speed2 = qa_q1_actor_number(entity, "speed2");
    closing = speed2 != 0 ? speed2 : entity->speed;
  }
  qa_q1_calc_move(game, entity, entity->pos1, closing, named_action(game, entity, "door_hit_bottom"));
}

void qa_q1_door_up(qa_q1_services *game, qa_q1_actor *entity) {
  if (entity->state != NULL && !strcmp(entity->state, "up")) return;
  if (entity->state != NULL && !strcmp(entity->state, "top")) {
    double due_seconds;
    qa_q1_bound_action callback;
    if (entity->wait >= 0 && (flags_of(entity) & 32) == 0)
      (due_seconds = fr(qa_q1_actor_number(entity, "ltime") + entity->wait), callback = named_action(game, entity, "door_go_down"), qa_q1_schedule_at(game, entity, due_seconds,
                        callback));
    return;
  }
  qa_q1_sound_defaults(game, entity->actor.id, door_sound(entity, true));
  entity->state = "up";
  qa_q1_calc_move(game, entity, entity->pos2, entity->speed, named_action(game, entity, "door_hit_top"));
  qa_q1_use_targets(game, entity, activator_of(entity));
}

static void key_message(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const char *key) {
  const double world_type = entity_world_type(game, entity);
  char need[64];
  (void)snprintf(need, sizeof need, "$qc_need_%s_%s", !strcmp(key, "q1:key/gold") ? "gold" : "silver",
                 world_type == 2 ? "keycard" : world_type == 1 ? "runekey" : "key");
  qa_q1_message(game, &other, need, true, NULL, 0);
  qa_q1_sound_defaults(game, entity->actor.id,
                       world_type == 2 ? "doors/basetry.wav" : world_type == 1 ? "doors/runetry.wav" : "doors/medtry.wav");
}
static void key_use_sound(qa_q1_services *game, qa_q1_actor *entity) {
  const double world_type = entity_world_type(game, entity);
  qa_q1_sound(game, entity->actor.id,
              world_type == 2 ? "doors/baseuse.wav" : world_type == 1 ? "doors/runeuse.wav" : "doors/meduse.wav",
              "item", 1, 1);
}

static void door_use(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  qa_q1_actor *master = entity->door_group_count ? entity->door_group[0] : entity;
  if (lock_key(master) != NULL) key_use_sound(game, master);
  const bool down = (flags_of(master) & 32) != 0 && master->state != NULL &&
                    (!strcmp(master->state, "up") || !strcmp(master->state, "top"));
  if (master->door_group_count == 0) {
    set_string(&master->message, "");
    set_activator(master, activator);
    if (down) qa_q1_door_down(game, master); else qa_q1_door_up(game, master);
    return;
  }
  for (size_t i = 0; i < master->door_group_count; i++) {
    qa_q1_actor *door = master->door_group[i];
    set_string(&door->message, "");
    set_activator(door, activator);
    if (down) qa_q1_door_down(game, door); else qa_q1_door_up(game, door);
  }
}

void qa_q1_spawn_door(qa_q1_services *game, qa_q1_actor *entity) {
  static const char *const KEY_SOUNDS[3][2] = {{"doors/medtry.wav", "doors/meduse.wav"},
                                               {"doors/runetry.wav", "doors/runeuse.wav"},
                                               {"doors/basetry.wav", "doors/baseuse.wav"}};
  const double world_type = entity_world_type(game, entity);
  if (world_type == 0 || world_type == 1 || world_type == 2)
    for (size_t i = 0; i < 2; i++) precache_sound(game, KEY_SOUNDS[(size_t)world_type][i]);
  static const char *const SOUNDS[5][2] = {
      {"misc/null.wav", "misc/null.wav"}, {"doors/drclos4.wav", "doors/doormv1.wav"},
      {"doors/hydro1.wav", "doors/hydro2.wav"}, {"doors/stndr1.wav", "doors/stndr2.wav"},
      {"doors/ddoor1.wav", "doors/ddoor2.wav"}};
  if (entity->sounds >= 0 && entity->sounds <= 4 && entity->sounds == floor(entity->sounds))
    for (size_t i = 0; i < 2; i++) precache_sound(game, SOUNDS[(size_t)entity->sounds][i]);
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 authored;
  const bool has_authored = authored_move_direction(game, entity, &authored);
  entity->movedir = has_authored ? authored : qa_q1_move_direction(body.angles, game);
  set_angles_zero(game, entity);
  entity->solid = game->source_door_keys && (flags_of(entity) & DOOR_NONSOLID) != 0 ? "none" : "bsp";
  entity->movement = "push";
  if (!entity->speed) entity->speed = 100;
  if (!entity->wait) entity->wait = 3;
  if (!entity->damage) entity->damage = 2;
  if ((flags_of(entity) & 24) != 0) entity->wait = -1;
  entity->pos1 = body.origin;
  const qa_vec3 size = qa_q1_vsub(body.bounds.max, body.bounds.min);
  const qa_vec3 absolute = v3(fabs(entity->movedir.x), fabs(entity->movedir.y), fabs(entity->movedir.z));
  const bool rerelease = game->options.edition != NULL && !strcmp(game->options.edition, "rerelease");
  const double distance = rerelease ? qa_q1_dot(absolute, size) : fabs(qa_q1_dot(entity->movedir, size));
  entity->pos2 = has_authored
                     ? qa_q1_vadd(entity->pos1, authored)
                     : qa_q1_vadd(entity->pos1,
                                  qa_q1_vscale(entity->movedir, distance - (qa_q1_actor_number(entity, "lip") ? qa_q1_actor_number(entity, "lip") : 8)));
  if ((flags_of(entity) & 1) != 0) {
    const qa_vec3 closed = entity->pos1;
    entity->pos1 = entity->pos2;
    entity->pos2 = closed;
    qa_q1_set_origin(game, entity, entity->pos1);
  }
  entity->use = qa_q1_callback_registry_use(game->named, entity, "door_use");
  entity->blocked = qa_q1_callback_registry_blocked(game->named, entity, "door_blocked");
  if (entity->max_health > 0) {
    qa_q1_actor_set_damageable(entity, true);
    entity->die = qa_q1_callback_registry_die(game->named, entity, "door_killed");
  }
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "door_touch");
}

void qa_q1_link_doors(qa_q1_services *game) {
  for (size_t m = 0; m < game->entity_count; m++) {
    qa_q1_actor *master = game->entities[m];
    if (master == NULL || master->classname == NULL || strcmp(master->classname, "func_door") != 0) continue;
    if (master->door_group_count > 0) continue;
    /* TS builds an unbounded array; a fixed 64 dropped later doors. */
    size_t group_count = 0, group_capacity = 8;
    qa_q1_actor **group = malloc(group_capacity * sizeof *group);
    if (group == NULL) qa_range_error("Out of memory");
    qa_cleanup group_cleanup; /* "cross connected doors" raises */
    qa_cleanup_push(&group_cleanup, free, group);
    group[group_count++] = master;
    qa_q1_body master_body = qa_q1_get_body(game, master);
    qa_vec3 min = master_body.bounds.min, max = master_body.bounds.max;
    if ((flags_of(master) & 4) == 0) {
      qa_q1_actor *current = master;
      for (size_t c = m + 1; c < game->entity_count; c++) {
        qa_q1_actor *candidate = game->entities[c];
        if (candidate == NULL || candidate->classname == NULL ||
            strcmp(candidate->classname, "func_door") != 0)
          continue;
        qa_bounds a = qa_q1_get_body(game, current).bounds;
        qa_bounds b = qa_q1_get_body(game, candidate).bounds;
        if (!qa_q1_overlaps(a, b))
          continue;
        if (candidate->door_group_count != 0) qa_range_error("cross connected doors");
        if (group_count == group_capacity) {
          qa_q1_actor **grown = realloc(group, group_capacity * 2 * sizeof *group);
          if (grown == NULL) qa_range_error("Out of memory");
          group = grown;
          group_cleanup.arg = group;
          group_capacity *= 2;
        }
        group[group_count++] = candidate;
        current = candidate;
        const qa_bounds bounds = qa_q1_get_body(game, candidate).bounds;
        min = v3(qa_js_min(min.x, bounds.min.x), qa_js_min(min.y, bounds.min.y), qa_js_min(min.z, bounds.min.z));
        max = v3(qa_js_max(max.x, bounds.max.x), qa_js_max(max.y, bounds.max.y), qa_js_max(max.z, bounds.max.z));
        if (candidate->targetname != NULL && strcmp(candidate->targetname, "") != 0)
          set_string(&master->targetname, candidate->targetname);
        if (candidate->message != NULL && strcmp(candidate->message, "") != 0)
          set_string(&master->message, candidate->message);
        if (candidate->max_health != 0) {
          master->max_health = candidate->max_health;
          qa_gameplay_authority_set_health(game->host.combat, master->actor, candidate->max_health);
        }
      }
    }
    for (size_t i = 0; i < group_count; i++) {
      qa_q1_actor **copy = malloc(group_count * sizeof *copy);
      if (!copy) continue;
      memcpy(copy, group, group_count * sizeof *copy);
      free(group[i]->door_group);
      group[i]->door_group = copy;
      group[i]->door_group_count = group_count;
    }
    qa_cleanup_pop(&group_cleanup);
    free(group);
    if ((flags_of(master) & (4 | 8 | 16)) != 0 || master->max_health > 0 ||
        (master->targetname != NULL && strcmp(master->targetname, "") != 0))
      continue;
    qa_q1_actor *trigger = qa_q1_create(game, "door_trigger", NULL, NULL);
    if (trigger == NULL) continue;
    trigger->solid = "trigger";
    trigger->has_trigger_bounds = true;
    trigger->trigger_bounds.min = qa_q1_vsub(min, v3(60, 60, 8));
    trigger->trigger_bounds.max = qa_q1_vadd(max, v3(60, 60, 8));
    qa_q1_set_bounds(game, trigger, trigger->trigger_bounds);
    trigger->has_owner = true;
    trigger->owner = master->actor.id;
    trigger->touch = qa_q1_callback_registry_touch(game->named, trigger, "door_trigger_touch");
  }
}

static const char *button_sound(const qa_q1_actor *entity) {
  static const char *const SOUNDS[4] = {"buttons/airbut1.wav", "buttons/switch21.wav",
                                        "buttons/switch02.wav", "buttons/switch04.wav"};
  if (entity->sounds >= 0 && entity->sounds <= 3 && entity->sounds == floor(entity->sounds))
    return SOUNDS[(size_t)entity->sounds];
  return NULL;
}

void qa_q1_spawn_button(qa_q1_services *game, qa_q1_actor *entity) {
  const char *sound = button_sound(entity);
  if (sound != NULL) precache_sound(game, sound);
  const qa_q1_body body = qa_q1_get_body(game, entity);
  entity->solid = "bsp";
  entity->movement = "push";
  if (!entity->speed) entity->speed = 40;
  if (!entity->wait) entity->wait = 1;
  qa_vec3 authored;
  const bool has_authored = authored_move_direction(game, entity, &authored);
  entity->movedir = has_authored ? authored : qa_q1_move_direction(body.angles, game);
  set_angles_zero(game, entity);
  entity->pos1 = body.origin;
  const qa_vec3 size = qa_q1_vsub(body.bounds.max, body.bounds.min);
  const qa_vec3 absolute = v3(fabs(entity->movedir.x), fabs(entity->movedir.y), fabs(entity->movedir.z));
  const bool rerelease = game->options.edition != NULL && !strcmp(game->options.edition, "rerelease");
  const double distance = rerelease ? qa_q1_dot(absolute, size) : fabs(qa_q1_dot(entity->movedir, size));
  entity->pos2 = has_authored
                     ? qa_q1_vadd(body.origin, authored)
                     : qa_q1_vadd(body.origin,
                                  qa_q1_vscale(entity->movedir,
                                               distance - (qa_q1_actor_number(entity, "lip") ? qa_q1_actor_number(entity, "lip") : 4)));
  entity->use = qa_q1_callback_registry_use(game->named, entity, "button_use");
  if (entity->max_health > 0) {
    qa_q1_actor_set_damageable(entity, true);
    entity->die = qa_q1_callback_registry_die(game->named, entity, "button_killed");
  } else {
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "button_touch");
  }
}

static bool secret_shootable(const qa_q1_actor *entity) {
  return (entity->targetname == NULL || !strcmp(entity->targetname, "")) || (flags_of(entity) & 16) != 0;
}

void qa_q1_spawn_secret_door(qa_q1_services *game, qa_q1_actor *entity) {
  static const char *const S1[3] = {"doors/latch2.wav", "doors/winch2.wav", "doors/drclos4.wav"};
  static const char *const S2[2] = {"doors/airdoor1.wav", "doors/airdoor2.wav"};
  static const char *const S03[2] = {"doors/basesec1.wav", "doors/basesec2.wav"};
  if (entity->sounds == 1) {
    for (size_t i = 0; i < 3; i++) precache_sound(game, S1[i]);
  } else if (entity->sounds == 2) {
    for (size_t i = 0; i < 2; i++) precache_sound(game, S2[i]);
  } else if (entity->sounds == 0 || entity->sounds == 3) {
    for (size_t i = 0; i < 2; i++) precache_sound(game, S03[i]);
  }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  entity->mangle = body.angles;
  set_angles_zero(game, entity);
  entity->solid = "bsp";
  entity->movement = "push";
  entity->speed = 50;
  if (!entity->wait) entity->wait = 5;
  if (!entity->damage) entity->damage = 2;
  entity->pos1 = body.origin;
  if (!entity->sounds) entity->sounds = 3;
  qa_q1_actor_set_damageable(entity, secret_shootable(entity));
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 10000);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "fd_secret_use");
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, "fd_secret_use");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "fd_secret_use");
  entity->blocked = qa_q1_callback_registry_blocked(game->named, entity, "fd_secret_blocked");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "fd_secret_touch");
}

void qa_q1_spawn_plat(qa_q1_services *game, qa_q1_actor *entity) {
  static const char *const S1[2] = {"plats/plat1.wav", "plats/plat2.wav"};
  static const char *const S02[2] = {"plats/medplat1.wav", "plats/medplat2.wav"};
  if (entity->sounds == 1) {
    for (size_t i = 0; i < 2; i++) precache_sound(game, S1[i]);
  } else if (entity->sounds == 0 || entity->sounds == 2) {
    for (size_t i = 0; i < 2; i++) precache_sound(game, S02[i]);
  }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 size = qa_q1_vsub(body.bounds.max, body.bounds.min);
  entity->solid = "bsp";
  entity->movement = "push";
  if (!entity->speed) entity->speed = 150;
  entity->pos1 = body.origin;
  entity->pos2 = qa_q1_vadd(body.origin,
                            v3(0, 0, -(qa_q1_actor_number(entity, "height") ? qa_q1_actor_number(entity, "height") : size.z - 8)));
  set_angles_zero(game, entity);
  entity->blocked = qa_q1_callback_registry_blocked(game->named, entity, "plat_crush");
  entity->use = qa_q1_callback_registry_use(game->named, entity, "plat_use");
  entity->activated = entity->targetname == NULL || !strcmp(entity->targetname, "");
  if (entity->activated) {
    entity->state = "bottom";
    qa_q1_set_origin(game, entity, entity->pos2);
  } else {
    entity->state = "up";
  }
  qa_q1_actor *trigger = qa_q1_create(game, "plat_trigger", NULL, NULL);
  if (trigger == NULL) return;
  trigger->solid = "trigger";
  qa_vec3 min = qa_q1_vadd(body.bounds.min, v3(25, 25, 0));
  qa_vec3 max = qa_q1_vsub(body.bounds.max, v3(25, 25, -8));
  min.z = max.z - (entity->pos1.z - entity->pos2.z + 8);
  if ((flags_of(entity) & 1) != 0) max.z = min.z + 8;
  if (size.x <= 50) {
    min.x = (body.bounds.min.x + body.bounds.max.x) / 2;
    max.x = min.x + 1;
  }
  if (size.y <= 50) {
    min.y = (body.bounds.min.y + body.bounds.max.y) / 2;
    max.y = min.y + 1;
  }
  trigger->has_trigger_bounds = true;
  trigger->trigger_bounds.min = min;
  trigger->trigger_bounds.max = max;
  qa_q1_set_bounds(game, trigger, trigger->trigger_bounds);
  trigger->has_owner = true;
  trigger->owner = entity->actor.id;
  trigger->touch = qa_q1_callback_registry_touch(game->named, trigger, "plat_center_touch");
}

static void door_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  qa_q1_actor *master = entity->door_group_count ? entity->door_group[0] : entity;
  if (master->attack_finished > game->time) return;
  master->attack_finished = game->time + 2;
  qa_q1_message(game, &other, master->message ? master->message : "", true, NULL, 0);
  if (master->message != NULL && master->message[0] != '\0') {
    qa_owned_actor owner;
    if (qa_session_actor_registry_resolve_owned(game->host.actors, other, &owner))
      qa_q1_sound_defaults(game, owner.id, "misc/talk.wav");
  }
  const char *key = lock_key(entity);
  if (key == NULL) return;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL) return;
  if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, key, 1)) {
    key_message(game, entity, other, key);
    return;
  }
  /* door_touch clears only this door and the next one in the link ring. */
  memset(&entity->touch, 0, sizeof entity->touch);
  if (master->door_group_count > 0) {
    size_t index = master->door_group_count; /* TS indexOf -1 */
    for (size_t i = 0; i < master->door_group_count; i++)
      if (master->door_group[i] == entity) { index = i; break; }
    /* (indexOf + 1) % length: -1 + 1 = 0 when absent. */
    const size_t next = index == master->door_group_count ? 0 : (index + 1) % master->door_group_count;
    memset(&master->door_group[next]->touch, 0, sizeof master->door_group[next]->touch);
  }
  door_use(game, master, &other);
}

static void button_fire(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  set_activator(entity, activator);
  if (entity->state != NULL && (!strcmp(entity->state, "up") || !strcmp(entity->state, "top"))) return;
  entity->state = "up";
  const char *sound = button_sound(entity);
  qa_q1_sound_defaults(game, entity->actor.id, sound ? sound : "buttons/airbut1.wav");
  qa_q1_calc_move(game, entity, entity->pos2, entity->speed, named_action(game, entity, "button_wait"));
}

static const char *secret_sound(const qa_q1_actor *entity, bool moving) {
  return entity->sounds == 1 ? (moving ? "doors/winch2.wav" : "doors/drclos4.wav")
         : entity->sounds == 2 ? (moving ? "doors/airdoor1.wav" : "doors/airdoor2.wav")
                               : (moving ? "doors/basesec1.wav" : "doors/basesec2.wav");
}

static void secret_fire(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 10000);
  if (entity->state == NULL || strcmp(entity->state, "bottom") != 0 || entity->has_move) return;
  set_string(&entity->message, "");
  qa_q1_use_targets(game, entity, activator);
  qa_q1_actor_set_damageable(entity, false);
  entity->state = "up";
  const qa_q1_basis basis = qa_q1_make_vectors(game, entity->mangle);
  const qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
  const qa_vec3 size = qa_q1_vsub(bounds.max, bounds.min);
  const double width = qa_q1_actor_number(entity, "t_width")
                           ? qa_q1_actor_number(entity, "t_width")
                           : fabs(qa_q1_dot((flags_of(entity) & 4) != 0 ? basis.up : basis.right, size));
  const double distance = qa_q1_actor_number(entity, "t_length")
                              ? qa_q1_actor_number(entity, "t_length")
                              : fabs(qa_q1_dot(basis.forward, size));
  const qa_vec3 direction = (flags_of(entity) & 4) != 0
                                ? qa_q1_vscale(basis.up, -width)
                                : qa_q1_vscale(basis.right, width * (1 - (flags_of(entity) & 2)));
  entity->dest1 = qa_q1_vadd(entity->pos1, direction);
  entity->dest2 = qa_q1_vadd(entity->dest1, qa_q1_vscale(basis.forward, distance));
  qa_q1_sound_defaults(game, entity->actor.id,
                       entity->sounds == 1 ? "doors/latch2.wav" : secret_sound(entity, false));
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, true));
  qa_q1_calc_move(game, entity, entity->dest1, entity->speed, named_action(game, entity, "fd_secret_move1"));
}

static const char *plat_sound(const qa_q1_actor *entity, bool moving) {
  return entity->sounds == 1 ? (moving ? "plats/plat1.wav" : "plats/plat2.wav")
                              : (moving ? "plats/medplat1.wav" : "plats/medplat2.wav");
}
static void plat_down(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "down";
  qa_q1_sound_defaults(game, entity->actor.id, plat_sound(entity, true));
  qa_q1_calc_move(game, entity, entity->pos2, entity->speed, named_action(game, entity, "plat_hit_bottom"));
}
static void plat_up(qa_q1_services *game, qa_q1_actor *entity) {
  entity->state = "up";
  qa_q1_sound_defaults(game, entity->actor.id, plat_sound(entity, true));
  qa_q1_calc_move(game, entity, entity->pos1, entity->speed, named_action(game, entity, "plat_hit_top"));
}

static void calc_move_done(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (!entity->has_move) qa_range_error("Q1 mover completion has no destination");
  const qa_q1_move move = entity->move;
  qa_q1_set_origin(game, entity, move.destination);
  qa_q1_body patch = qa_q1_get_body(game, entity);
  patch.velocity = QA_Q1_ZERO;
  qa_q1_set_body_patch(game, entity, &patch, false, false, true, false, false);
  entity->next_think = -1;
  entity->has_move = false;
  qa_q1_call_action(game, entity, move.done);
}
static void door_hit_bottom(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "bottom";
  qa_q1_sound_defaults(game, entity->actor.id, door_sound(entity, false));
}
static void door_hit_top(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  double due_seconds;
  qa_q1_bound_action callback;
  (void)ctx;
  entity->state = "top";
  qa_q1_sound_defaults(game, entity->actor.id, door_sound(entity, false));
  if (entity->wait >= 0 && (flags_of(entity) & 32) == 0)
    (due_seconds = fr(qa_q1_actor_number(entity, "ltime") + entity->wait), callback = named_action(game, entity, "door_go_down"), qa_q1_schedule_at(game, entity, due_seconds,
                      callback));
}
static void door_go_down(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_door_down(game, entity);
}
static void door_use_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other;
  door_use(game, entity, activator);
}
static void door_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "crush", NULL);
  if (game->source_door_keys && (flags_of(entity) & DOOR_CRUSHER) != 0) return;
  if (entity->wait >= 0) {
    if (entity->state != NULL && !strcmp(entity->state, "down")) qa_q1_door_up(game, entity);
    else qa_q1_door_down(game, entity);
  }
}
static void door_killed(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  qa_q1_actor *master = entity->door_group_count ? entity->door_group[0] : entity;
  qa_gameplay_authority_set_health(game->host.combat, master->actor, master->max_health);
  qa_q1_actor_set_damageable(master, false);
  door_use(game, master, attacker);
}
static void door_trigger_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                               const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (qa_q1_health(game, other) <= 0 || entity->attack_finished > game->time) return;
  qa_q1_actor *master = entity->has_owner ? qa_q1_get_entity(game, &entity->owner) : NULL;
  if (master == NULL) return;
  entity->attack_finished = game->time + 1;
  door_use(game, master, &other);
}
static void button_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                       const qa_actor_id *activator) {
  (void)ctx; (void)other;
  button_fire(game, entity, activator);
}
static void button_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  const char *key = game->source_door_keys ? lock_key(entity) : NULL;
  if (key != NULL) {
    qa_q1_player_state *player = qa_q1_player(game, other);
    if (player == NULL) return;
    if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, key, 1)) {
      key_message(game, entity, other, key);
      return;
    }
    key_use_sound(game, entity);
  }
  button_fire(game, entity, &other);
}
static void button_killed(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
  qa_q1_actor_set_damageable(entity, false);
  button_fire(game, entity, attacker);
}
static void button_wait(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  double due_seconds;
  qa_q1_bound_action callback;
  (void)ctx;
  entity->state = "top";
  if (entity->wait >= 0)
    (due_seconds = fr(qa_q1_actor_number(entity, "ltime") + entity->wait), callback = named_action(game, entity, "button_return"), qa_q1_schedule_at(game, entity, due_seconds,
                      callback));
  qa_q1_use_targets(game, entity, activator_of(entity));
  entity->frame = 1;
}
static void button_return(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "down";
  entity->frame = 0;
  if (entity->max_health > 0) qa_q1_actor_set_damageable(entity, true);
  qa_q1_calc_move(game, entity, entity->pos1, entity->speed, named_action(game, entity, "button_done"));
}
static void button_done(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game;
  entity->state = "bottom";
}
static void secret_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                       const qa_actor_id *activator) {
  (void)ctx; (void)other;
  secret_fire(game, entity, activator);
}
static void secret_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)damage;
  secret_fire(game, entity, attacker);
}
static void secret_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  secret_fire(game, entity, attacker);
}
static void secret_move1(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, false));
  double due_seconds = fr(qa_q1_actor_number(entity, "ltime") + 1);
  qa_q1_bound_action callback = named_action(game, entity, "fd_secret_move2");
  qa_q1_schedule_at(game, entity, due_seconds, callback);
}
static void secret_move2(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, true));
  qa_q1_calc_move(game, entity, entity->dest2, entity->speed, named_action(game, entity, "fd_secret_move3"));
}
static void secret_move3(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  double due_seconds;
  qa_q1_bound_action callback;
  (void)ctx;
  entity->state = "top";
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, false));
  if ((flags_of(entity) & 1) == 0)
    (due_seconds = fr(qa_q1_actor_number(entity, "ltime") + entity->wait), callback = named_action(game, entity, "fd_secret_move4"), qa_q1_schedule_at(game, entity, due_seconds,
                      callback));
}
static void secret_move4(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, true));
  entity->state = "down";
  qa_q1_calc_move(game, entity, entity->dest1, entity->speed, named_action(game, entity, "fd_secret_move5"));
}
static void secret_move5(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, false));
  double due_seconds = fr(qa_q1_actor_number(entity, "ltime") + 1);
  qa_q1_bound_action callback = named_action(game, entity, "fd_secret_move6");
  qa_q1_schedule_at(game, entity, due_seconds, callback);
}
static void secret_move6(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, true));
  qa_q1_calc_move(game, entity, entity->pos1, entity->speed, named_action(game, entity, "fd_secret_done"));
}
static void secret_done(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "bottom";
  qa_q1_actor_set_damageable(entity, secret_shootable(entity));
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 10000);
  qa_q1_sound_defaults(game, entity->actor.id, secret_sound(entity, false));
}
static void secret_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx;
  if (entity->attack_finished > game->time) return;
  entity->attack_finished = game->time + 0.5;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "crush", NULL);
}
static void secret_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other) || entity->attack_finished > game->time) return;
  entity->attack_finished = game->time + 2;
  qa_q1_message(game, &other, entity->message ? entity->message : "", true, NULL, 0);
}
static void plat_hit_bottom(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "bottom";
  qa_q1_sound_defaults(game, entity->actor.id, plat_sound(entity, false));
}
static void plat_hit_top(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "top";
  qa_q1_sound_defaults(game, entity->actor.id, plat_sound(entity, false));
  double due_seconds = fr(qa_q1_actor_number(entity, "ltime") + 3);
  qa_q1_bound_action callback = named_action(game, entity, "plat_go_down");
  qa_q1_schedule_at(game, entity, due_seconds, callback);
}
static void plat_crush(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 1, NULL, "direct", "crush", NULL);
  if (entity->state != NULL && !strcmp(entity->state, "up")) plat_down(NULL, game, entity);
  else plat_up(game, entity);
}
static void plat_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  if (entity->activated) return;
  entity->activated = true;
  plat_down(NULL, game, entity);
}
static void plat_center_touch(void *ctx, qa_q1_services *game, qa_q1_actor *trigger, qa_actor_id other,
                              const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  qa_q1_actor *entity = trigger->has_owner ? qa_q1_get_entity(game, &trigger->owner) : NULL;
  if (entity == NULL) return;
  if (entity->state != NULL && !strcmp(entity->state, "bottom")) {
    plat_up(game, entity);
  } else if (entity->state != NULL && !strcmp(entity->state, "top")) {
    double due_seconds = fr(qa_q1_actor_number(entity, "ltime") + 1);
    qa_q1_bound_action callback = named_action(game, entity, "plat_go_down");
    qa_q1_schedule_at(game, entity, due_seconds, callback);
  }
}

static void reg(qa_q1_services *game, const char *name, qa_q1_callback_handlers handlers) {
  qa_q1_callback_registry_register(game->named, name, handlers);
}

void qa_q1_register_mover_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers h = {0};
  h.action = calc_move_done; reg(game, "SUB_CalcMoveDone", h);
  h = (qa_q1_callback_handlers){0}; h.action = door_go_down; reg(game, "door_go_down", h);
  h = (qa_q1_callback_handlers){0}; h.action = door_hit_bottom; reg(game, "door_hit_bottom", h);
  h = (qa_q1_callback_handlers){0}; h.action = door_hit_top; reg(game, "door_hit_top", h);
  h = (qa_q1_callback_handlers){0}; h.use = door_use_cb; reg(game, "door_use", h);
  h = (qa_q1_callback_handlers){0}; h.blocked = door_blocked; reg(game, "door_blocked", h);
  h = (qa_q1_callback_handlers){0}; h.die = door_killed; reg(game, "door_killed", h);
  h = (qa_q1_callback_handlers){0}; h.touch = door_touch; reg(game, "door_touch", h);
  h = (qa_q1_callback_handlers){0}; h.touch = door_trigger_touch; reg(game, "door_trigger_touch", h);
  h = (qa_q1_callback_handlers){0}; h.use = button_use; reg(game, "button_use", h);
  h = (qa_q1_callback_handlers){0}; h.touch = button_touch; reg(game, "button_touch", h);
  h = (qa_q1_callback_handlers){0}; h.die = button_killed; reg(game, "button_killed", h);
  h = (qa_q1_callback_handlers){0}; h.action = button_wait; reg(game, "button_wait", h);
  h = (qa_q1_callback_handlers){0}; h.action = button_return; reg(game, "button_return", h);
  h = (qa_q1_callback_handlers){0}; h.action = button_done; reg(game, "button_done", h);
  h = (qa_q1_callback_handlers){0}; h.use = secret_use; h.pain = secret_pain; h.die = secret_die;
  reg(game, "fd_secret_use", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_move1; reg(game, "fd_secret_move1", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_move2; reg(game, "fd_secret_move2", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_move3; reg(game, "fd_secret_move3", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_move4; reg(game, "fd_secret_move4", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_move5; reg(game, "fd_secret_move5", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_move6; reg(game, "fd_secret_move6", h);
  h = (qa_q1_callback_handlers){0}; h.action = secret_done; reg(game, "fd_secret_done", h);
  h = (qa_q1_callback_handlers){0}; h.blocked = secret_blocked; reg(game, "fd_secret_blocked", h);
  h = (qa_q1_callback_handlers){0}; h.touch = secret_touch; reg(game, "fd_secret_touch", h);
  h = (qa_q1_callback_handlers){0}; h.action = plat_down; reg(game, "plat_go_down", h);
  h = (qa_q1_callback_handlers){0}; h.action = plat_hit_bottom; reg(game, "plat_hit_bottom", h);
  h = (qa_q1_callback_handlers){0}; h.action = plat_hit_top; reg(game, "plat_hit_top", h);
  h = (qa_q1_callback_handlers){0}; h.blocked = plat_crush; reg(game, "plat_crush", h);
  h = (qa_q1_callback_handlers){0}; h.use = plat_use; reg(game, "plat_use", h);
  h = (qa_q1_callback_handlers){0}; h.touch = plat_center_touch; reg(game, "plat_center_touch", h);
}
