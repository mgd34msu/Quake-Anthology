/*
 * Port of src/content/q1/foundation/spawns.ts. GPL-2.0-or-later.
 * UnknownQ1SpawnError is qa_raise with that name and the exact TS
 * message; the foreign-world map load catches it as TS does.
 * qa_q1_spawn_known is a side-effect-free classname check for callers
 * that only ask whether the foundation itself spawns a class.
 */
#include "qa/content/q1/foundation/spawns.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/precache_world.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static char last_unknown_spawn[128];
const char *qa_q1_last_unknown_spawn(void) {
  return last_unknown_spawn[0] ? last_unknown_spawn : NULL;
}
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
/* TS game.host.bodies.read(actor); false = null. */
static bool body_of(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!body.has_value) return false;
  *out = qa_q1_body_from_state(body.value);
  return true;
}
/* TS game.host.combat.read(actor)?.canTakeDamage. */
static bool can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, actor);
  return combat.has_value && combat.value.can_take_damage;
}
static void set_activator(qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator) entity->activator = *activator;
}
static const qa_actor_id *activator_of(const qa_q1_actor *entity) {
  return entity->has_activator ? &entity->activator : NULL;
}
static qa_q1_bound_action named_action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(game->named, entity, name);
}
static void precache_sound(qa_q1_services *game, const char *path) {
  if (qa_q1_services_uses_id1(game)) (void)qa_q1_services_precache_sound(game, path);
}
static void set_body_angles(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 angles) {
  qa_q1_body patch = qa_q1_get_body(game, entity);
  patch.angles = angles;
  qa_q1_set_body_patch(game, entity, &patch, false, true, false, false, false);
}
static void emit_ambient(qa_q1_services *game, qa_vec3 origin, const char *path, double volume) {
  qa_q1_event event = {0};
  event.kind = "ambient";
  event.has_origin = true;
  event.origin = origin;
  event.path = path;
  event.volume = volume;
  event.attenuation = 3;
  qa_q1_emit(game, event);
}
static void emit_lightstyle(qa_q1_services *game, double style, const char *pattern) {
  qa_q1_event event = {0};
  event.kind = "lightstyle";
  event.style = style;
  event.pattern = pattern;
  qa_q1_emit(game, event);
}

static void init_trigger(qa_q1_services *game, qa_q1_actor *entity) {
  const qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  entity->movedir = angles.x == 0 && angles.y == 0 && angles.z == 0
                        ? QA_Q1_ZERO
                        : qa_q1_move_direction(angles, game);
  set_body_angles(game, entity, QA_Q1_ZERO);
  entity->solid = "trigger";
  entity->movement = "none";
  set_string(&entity->model, "");
}

static void spawn_multi(qa_q1_services *game, qa_q1_actor *entity) {
  init_trigger(game, entity);
  const bool secret = !strcmp(entity->classname, "trigger_secret");
  const bool once = secret || !strcmp(entity->classname, "trigger_once");
  entity->wait = once ? -1 : (entity->wait ? entity->wait : 0.2);
  if (secret) {
    game->total_secrets += 1;
    if (!entity->message || !entity->message[0]) set_string(&entity->message, "$qc_found_secret");
    if (!entity->sounds) entity->sounds = 1;
  }
  const char *sound = entity->sounds == 1 ? "misc/secret.wav"
                      : entity->sounds == 2 ? "misc/talk.wav"
                      : entity->sounds == 3 ? "misc/trigger1.wav" : NULL;
  if (secret && (entity->sounds == 1 || entity->sounds == 2) && sound) precache_sound(game, sound);
  if (sound) precache_sound(game, sound);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "multi_use");
  if (entity->max_health > 0) {
    if ((flags_of(entity) & 1) != 0) qa_range_error("health and notouch do not make sense");
    qa_q1_actor_set_damageable(entity, true);
    entity->solid = "bbox";
    entity->die = qa_q1_callback_registry_die(game->named, entity, "multi_killed");
  } else if ((flags_of(entity) & 1) == 0) {
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "multi_touch");
  }
}

static void spawn_counter(qa_q1_services *game, qa_q1_actor *entity) {
  set_string(&entity->model, "");
  if (!entity->count) entity->count = 2;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "counter_use");
}

static void spawn_teleport(qa_q1_services *game, qa_q1_actor *entity) {
  init_trigger(game, entity);
  if (!entity->target || !strcmp(entity->target, ""))
    qa_range_error("trigger_teleport has no target");
  entity->use = qa_q1_callback_registry_use(game->named, entity, "teleport_use");
  if ((flags_of(entity) & 2) == 0) {
    precache_sound(game, "ambience/hum1.wav");
    const qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
    emit_ambient(game, qa_q1_vscale(qa_q1_vadd(bounds.min, bounds.max), 0.5), "ambience/hum1.wav", 0.5);
  }
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "teleport_touch");
}

static void spawn_light(qa_q1_services *game, qa_q1_actor *entity) {
  qa_vec3 origin2;
  const char * path;
  if (!strcmp(entity->classname, "light") && (!entity->targetname || !strcmp(entity->targetname, ""))) {
    qa_q1_remove(game, entity);
    return;
  }
  const double style = qa_q1_actor_number(entity, "style");
  if (style >= 32 && strcmp(entity->classname, "light_fluorospark") != 0) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "light_use");
    emit_lightstyle(game, style, (flags_of(entity) & 1) != 0 ? "a" : "m");
  }
  if (!strcmp(entity->classname, "light_fluoro")) precache_sound(game, "ambience/fl_hum1.wav");
  if (!strcmp(entity->classname, "light_fluorospark")) precache_sound(game, "ambience/buzz1.wav");
  if (!strcmp(entity->classname, "light_fluoro") || !strcmp(entity->classname, "light_fluorospark"))
    (origin2 = qa_q1_get_body(game, entity).origin, path = !strcmp(entity->classname, "light_fluoro") ? "ambience/fl_hum1.wav" : "ambience/buzz1.wav", emit_ambient(game, origin2,
                 path, 0.5));
}

static void spawn_barrel(qa_q1_services *game, qa_q1_actor *entity) {
  const bool box2 = !strcmp(entity->classname, "misc_explobox2");
  set_string(&entity->model, box2 ? "maps/b_exbox2.bsp" : "maps/b_explob.bsp");
  if (qa_q1_services_uses_id1(game)) (void)qa_q1_services_precache_model(game, entity->model);
  if (qa_q1_services_uses_id1(game)) (void)qa_q1_services_precache_sound(game, "weapons/r_exp3.wav");
  entity->solid = "bbox";
  entity->movement = "none";
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 20);
  const qa_bounds bounds = {QA_Q1_ZERO, v3(32, 32, box2 ? 32 : 64)};
  qa_q1_set_bounds(game, entity, bounds);
  entity->die = qa_q1_callback_registry_die(game->named, entity, "barrel_die");
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 start = qa_q1_vadd(body.origin, v3(0, 0, 2));
  qa_q1_body patch = body;
  patch.origin = start;
  qa_q1_set_body_patch(game, entity, &patch, true, false, false, false, false);
  qa_q1_trace_request request = {start, qa_q1_vadd(start, v3(0, 0, -256)), body.bounds,
                                 true, entity->actor.id, true, false, false};
  const qa_q1_trace trace = qa_q1_host_trace(game, request);
  if (trace.fraction < 1 && !trace.all_solid) {
    patch = qa_q1_get_body(game, entity);
    patch.origin = trace.end;
    patch.has_ground = trace.has_actor;
    if (trace.has_actor) patch.ground = trace.actor;
    qa_q1_set_body_patch(game, entity, &patch, true, false, false, false, true);
    entity->movement_flags = (double)(qa_js_to_int32(entity->movement_flags) | 512);
    if (start.z - trace.end.z > 250) qa_q1_remove(game, entity);
  }
}

static double timer_interval(qa_q1_services *game, qa_q1_actor *entity) {
  const double random = qa_q1_actor_number(entity, "random");
  return qa_js_max(0.05, entity->wait + (random > 0 ? qa_q1_host_random(game) * random : 0));
}

static bool is_pickup_classname(const char *name, const qa_q1_actor *entity) {
  if (!strcmp(name, "item_health") || !strcmp(name, "item_armor1") || !strcmp(name, "item_armor2") ||
      !strcmp(name, "item_armorInv") || !strcmp(name, "item_shells") || !strcmp(name, "item_spikes") ||
      !strcmp(name, "item_rockets") || !strcmp(name, "item_cells") || !strcmp(name, "item_key1") ||
      !strcmp(name, "item_key2") || !strcmp(name, "item_artifact_invulnerability") ||
      !strcmp(name, "item_artifact_invisibility") || !strcmp(name, "item_artifact_envirosuit") ||
      !strcmp(name, "item_artifact_super_damage") || !strcmp(name, "item_backpack"))
    return true;
  if (!strcmp(name, "item_weapon"))
    return entity == NULL || (flags_of(entity) & (1 | 2 | 4)) != 0;
  return !strcmp(name, "weapon_supershotgun") || !strcmp(name, "weapon_nailgun") ||
         !strcmp(name, "weapon_supernailgun") || !strcmp(name, "weapon_grenadelauncher") ||
         !strcmp(name, "weapon_rocketlauncher") || !strcmp(name, "weapon_lightning");
}

static const char *const KNOWN_SPAWNS[] = {
    "worldspawn", "func_door", "func_button", "func_door_secret", "func_plat", "func_wall",
    "trigger_once", "trigger_multiple", "trigger_secret", "trigger_activate_coop_spawns",
    "trigger_counter", "trigger_relay", "trigger_teleport", "trigger_changelevel", "trigger_hurt",
    "trigger_push", "misc_teleporter_dest", "target_speaker", "target_relay", "target_delay",
    "target_remove_powerups", "func_timer", "trigger_always", "func_static", "func_bobbing",
    "misc_portal_surface", "misc_portal_camera", "info_teleport_destination", "testplayerstart",
    "info_player_start", "info_player_coop", "info_player_deathmatch", "info_player_start2",
    "info_intermission", "info_notnull", "target_location", "target_position", "info_null",
    "info_player_intermission", "info_player_team1", "info_player_team2", "info_player_single",
    "info_spectator", "info_camp", "info_podium", "misc_model", "path_corner", "light",
    "light_fluoro", "light_fluorospark", "ambient_comp_hum", "ambient_drone", "misc_explobox",
    "misc_explobox2"};

bool qa_q1_spawn_known(qa_q1_services *game, const char *classname, qa_q1_actor *entity) {
  (void)game;
  if (classname == NULL) return false;
  if (is_pickup_classname(classname, entity)) return true;
  for (size_t i = 0; i < sizeof KNOWN_SPAWNS / sizeof *KNOWN_SPAWNS; i++)
    if (!strcmp(KNOWN_SPAWNS[i], classname)) return true;
  return false;
}

void qa_q1_spawn_map_actor(qa_q1_services *game, qa_q1_actor *entity) {
  if (qa_q1_spawn_pickup(game, entity)) return;
  const char *name = entity->classname ? entity->classname : "";
  if (!strcmp(name, "worldspawn")) {
    game->world = entity;
    game->world_type = qa_q1_actor_number(entity, "worldtype");
    entity->solid = "bsp";
    if (qa_q1_services_uses_id1(game)) qa_q1_precache_world(game);
    for (size_t i = 0; i < game->world_precache_count; i++)
      game->world_precaches[i].fn(game->world_precaches[i].ctx, game);
    static const char *const STYLES[12] = {
        "m", "mmnmmommommnonmmonqnmmo", "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
        "mmmmmaaaaammmmmaaaaaabcdefgabcdefg", "mamamamamama", "jklmnopqrstuvwxyzyxwvutsrqponmlkj",
        "nmonqnmomnmomomno", "mmmaaaabcdefgmmmmaaaammmaamm", "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",
        "aaaaaaaazzzzzzzz", "mmamammmmammamamaaamammma", "abcdefghijklmnopqrrqponmlkjihgfedcba"};
    for (size_t style = 0; style < 12; style++) emit_lightstyle(game, (double)style, STYLES[style]);
    return;
  }
  if (!strcmp(name, "func_door")) { qa_q1_spawn_door(game, entity); return; }
  if (!strcmp(name, "func_button")) { qa_q1_spawn_button(game, entity); return; }
  if (!strcmp(name, "func_door_secret")) { qa_q1_spawn_secret_door(game, entity); return; }
  if (!strcmp(name, "func_plat")) { qa_q1_spawn_plat(game, entity); return; }
  if (!strcmp(name, "func_wall")) {
    entity->solid = "bsp";
    entity->movement = "push";
    set_body_angles(game, entity, QA_Q1_ZERO);
    entity->use = qa_q1_callback_registry_use(game->named, entity, "func_wall_use");
    return;
  }
  if (!strcmp(name, "trigger_once") || !strcmp(name, "trigger_multiple") || !strcmp(name, "trigger_secret"))
    { spawn_multi(game, entity); return; }
  if (!strcmp(name, "trigger_activate_coop_spawns")) {
    init_trigger(game, entity);
    entity->use = qa_q1_callback_registry_use(game->named, entity, "activate_coop_spawns_use");
    if ((flags_of(entity) & 1) == 0)
      entity->touch = qa_q1_callback_registry_touch(game->named, entity, "activate_coop_spawns_touch");
    return;
  }
  if (!strcmp(name, "trigger_counter")) { spawn_counter(game, entity); return; }
  if (!strcmp(name, "trigger_relay") || !strcmp(name, "target_relay")) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "trigger_relay_use");
    return;
  }
  if (!strcmp(name, "trigger_teleport")) { spawn_teleport(game, entity); return; }
  if (!strcmp(name, "trigger_changelevel")) {
    init_trigger(game, entity);
    if (!strcmp(qa_q1_actor_text(entity, "map"), "")) qa_range_error("changelevel trigger has no map");
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "changelevel_touch");
    return;
  }
  if (!strcmp(name, "trigger_hurt")) {
    init_trigger(game, entity);
    if (!entity->damage) entity->damage = 5;
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "hurt_touch");
    return;
  }
  if (!strcmp(name, "trigger_push")) {
    init_trigger(game, entity);
    if (entity->target && strcmp(entity->target, "") != 0) {
      entity->touch = qa_q1_callback_registry_touch(game->named, entity, "q3_push_touch");
      return;
    }
    precache_sound(game, "ambience/windfly.wav");
    if (!entity->speed) entity->speed = 1000;
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "push_touch");
    return;
  }
  if (!strcmp(name, "misc_teleporter_dest")) {
    entity->mangle = qa_q1_get_body(game, entity).angles;
    set_body_angles(game, entity, QA_Q1_ZERO);
    return;
  }
  if (!strcmp(name, "target_speaker")) {
    qa_vec3 origin2;
    const char * path;
    entity->use = qa_q1_callback_registry_use(game->named, entity, "speaker_use");
    if ((flags_of(entity) & 1) != 0 && strcmp(qa_q1_actor_text(entity, "noise"), "") != 0)
      (origin2 = qa_q1_get_body(game, entity).origin, path = qa_q1_actor_text(entity, "noise"), emit_ambient(game, origin2, path, 1));
    return;
  }
  if (!strcmp(name, "target_delay")) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "delay_use");
    return;
  }
  if (!strcmp(name, "target_remove_powerups")) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "remove_powerups_use");
    return;
  }
  if (!strcmp(name, "func_timer")) {
    if (!entity->wait) entity->wait = 1;
    entity->activated = true;
    entity->use = qa_q1_callback_registry_use(game->named, entity, "timer_use");
    qa_q1_actor * entity2 = entity;
    double delay2 = timer_interval(game, entity);
    qa_q1_bound_action callback = named_action(game, entity, "timer_fire");
    qa_q1_schedule(game, entity2, delay2, callback);
    return;
  }
  if (!strcmp(name, "trigger_always")) {
    qa_q1_schedule(game, entity, entity->delay > 0 ? entity->delay : 0.1,
                   named_action(game, entity, "always_fire"));
    return;
  }
  if (!strcmp(name, "func_static")) {
    entity->solid = "bsp";
    entity->movement = "push";
    entity->use = qa_q1_callback_registry_use(game->named, entity, "static_use");
    return;
  }
  if (!strcmp(name, "func_bobbing")) {
    entity->solid = "bsp";
    entity->movement = "push";
    const qa_q1_body body = qa_q1_get_body(game, entity);
    entity->movedir = body.angles.x == 0 && body.angles.y == 0 && body.angles.z == 0
                          ? v3(0, 0, 1)
                          : qa_q1_move_direction(body.angles, game);
    set_body_angles(game, entity, QA_Q1_ZERO);
    entity->pos1 = body.origin;
    const double height = qa_q1_actor_number(entity, "height") ? qa_q1_actor_number(entity, "height") : 32;
    entity->pos2 = qa_q1_vadd(body.origin, qa_q1_vscale(entity->movedir, height));
    entity->speed = 2 * height / qa_js_max(qa_q1_actor_number(entity, "speed") ? qa_q1_actor_number(entity, "speed") : 4, 0.1);
    entity->state = "up";
    qa_q1_calc_move(game, entity, entity->pos2, entity->speed, named_action(game, entity, "bob_top"));
    return;
  }
  if (!strcmp(name, "misc_portal_surface") || !strcmp(name, "misc_portal_camera")) return;
  if (!strcmp(name, "info_teleport_destination")) {
    entity->mangle = qa_q1_get_body(game, entity).angles;
    set_body_angles(game, entity, QA_Q1_ZERO);
    qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_get_body(game, entity).origin, v3(0, 0, 27)));
    if (!entity->targetname || !strcmp(entity->targetname, ""))
      qa_range_error("teleport destination has no targetname");
    return;
  }
  static const char *const INERT[] = {
      "testplayerstart", "info_player_start", "info_player_coop", "info_player_deathmatch",
      "info_player_start2", "info_intermission", "info_notnull", "target_location", "target_position",
      "info_null", "info_player_intermission", "info_player_team1", "info_player_team2",
      "info_player_single", "info_spectator", "info_camp", "info_podium"};
  for (size_t i = 0; i < sizeof INERT / sizeof *INERT; i++)
    if (!strcmp(name, INERT[i])) return;
  if (!strcmp(name, "misc_model")) {
    set_string(&entity->model, qa_q1_actor_text(entity, "model"));
    if (strcmp(entity->model, "") != 0 && qa_q1_services_uses_id1(game))
      (void)qa_q1_services_precache_model(game, entity->model);
    return;
  }
  if (!strcmp(name, "path_corner")) {
    if (!entity->targetname || !strcmp(entity->targetname, ""))
      qa_range_error("monster_movetarget has no targetname");
    entity->solid = "trigger";
    const qa_bounds bounds = {v3(-8, -8, -8), v3(8, 8, 8)};
    qa_q1_set_bounds(game, entity, bounds);
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "movetarget_touch");
    return;
  }
  if (!strcmp(name, "light") || !strcmp(name, "light_fluoro") || !strcmp(name, "light_fluorospark"))
    { spawn_light(game, entity); return; }
  if (!strcmp(name, "ambient_comp_hum") || !strcmp(name, "ambient_drone")) {
    const bool hum = !strcmp(name, "ambient_comp_hum");
    precache_sound(game, hum ? "ambience/comp1.wav" : "ambience/drone6.wav");
    emit_ambient(game, qa_q1_get_body(game, entity).origin, hum ? "ambience/comp1.wav" : "ambience/drone6.wav",
                 hum ? 1 : 0.5);
    return;
  }
  if (!strcmp(name, "misc_explobox") || !strcmp(name, "misc_explobox2")) { spawn_barrel(game, entity); return; }
  (void)snprintf(last_unknown_spawn, sizeof last_unknown_spawn, "%s", name);
  char ordinal[32];
  if (entity->has_source_ordinal) (void)snprintf(ordinal, sizeof ordinal, "%s", QA_JS_NUMBER(entity->source_ordinal));
  else (void)snprintf(ordinal, sizeof ordinal, "null");
  /* TS throw new UnknownQ1SpawnError: a distinct class the foreign-world map
   * load catches by name (qa_q1_last_unknown_spawn is error.classname). */
  qa_raise("UnknownQ1SpawnError", 0, 0, "Q1 official spawn not yet implemented: %s at source entity %s", name,
           ordinal);
}

qa_q1_actor *qa_q1_spawn_teleport_fog(qa_q1_services *game, qa_vec3 origin) {
  qa_q1_actor *fog = qa_q1_create(game, "teleport_fog", NULL, NULL);
  if (fog == NULL) return NULL;
  set_string(&fog->classname, "");
  qa_q1_body patch = qa_q1_get_body(game, fog);
  patch.origin = origin;
  qa_q1_set_body_patch(game, fog, &patch, true, false, false, false, false);
  qa_q1_schedule(game, fog, 0.2, named_action(game, fog, "play_teleport"));
  qa_q1_effect(game, "teleport", origin, NULL, 1);
  return fog;
}

qa_q1_actor *qa_q1_spawn_teledeath(qa_q1_services *game, qa_vec3 origin, qa_actor_id owner) {
  qa_q1_body body;
  if (!body_of(game, owner, &body)) qa_range_error("Teledeath owner has no shared body");
  qa_q1_actor *death = qa_q1_create(game, "teledeath", NULL, NULL);
  if (death == NULL) return NULL;
  death->solid = "trigger";
  death->has_owner = true;
  death->owner = owner;
  qa_q1_body patch = qa_q1_get_body(game, death);
  patch.origin = origin;
  patch.bounds.min = qa_q1_vsub(body.bounds.min, v3(1, 1, 1));
  patch.bounds.max = qa_q1_vadd(body.bounds.max, v3(1, 1, 1));
  qa_q1_set_body_patch(game, death, &patch, true, false, false, true, false);
  death->touch = qa_q1_callback_registry_touch(game->named, death, "tdeath_touch");
  qa_q1_schedule(game, death, 0.2, named_action(game, death, "SUB_Remove"));
  qa_q1_link(game, death);
  game->force_retouch = 2;
  return death;
}

static void multi_fire(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  const bool secret = !strcmp(entity->classname, "trigger_secret");
  const char *sound = entity->sounds == 1 ? "misc/secret.wav"
                      : entity->sounds == 2 ? "misc/talk.wav"
                      : entity->sounds == 3 ? "misc/trigger1.wav" : "";
  if (entity->next_think > game->time || !qa_q1_live(game, entity)) return;
  if (secret) {
    if (!qa_q1_is_player(game, activator)) return;
    game->found_secrets += 1;
    qa_q1_event event = {0};
    event.kind = "secret";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_secrets;
    event.found = game->found_secrets;
    qa_q1_emit(game, event);
    if (game->options.edition && !strcmp(game->options.edition, "rerelease") && activator) {
      qa_q1_event achievement = {0};
      achievement.kind = "achievement";
      achievement.has_player = true;
      achievement.player = *activator;
      achievement.id = "ACH_FIND_SECRET";
      qa_q1_emit(game, achievement);
    }
  }
  set_activator(entity, activator);
  qa_q1_actor_set_damageable(entity, false);
  if (strcmp(sound, "") != 0) qa_q1_sound_defaults(game, entity->actor.id, sound);
  qa_q1_use_targets(game, entity, activator);
  if (!qa_q1_live(game, entity)) return;
  if (game->options.edition != NULL && !strcmp(game->options.edition, "rerelease") &&
      qa_q1_is_player(game, activator) && game->map_name != NULL && !strcmp(game->map_name, "e2m3") &&
      entity->message != NULL && !strcmp(entity->message, "$map_dopefish") && activator != NULL) {
    qa_q1_event achievement = {0};
    achievement.kind = "achievement";
    achievement.has_player = true;
    achievement.player = *activator;
    achievement.id = "ACH_FIND_DOPEFISH";
    qa_q1_emit(game, achievement);
  }
  if (entity->wait > 0) {
    qa_q1_schedule(game, entity, entity->wait, named_action(game, entity, "multi_wait"));
    return;
  }
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_schedule(game, entity, 0.1, named_action(game, entity, "SUB_Remove"));
}

static void teleport_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->targetname && strcmp(entity->targetname, "") != 0 && entity->next_think < game->time &&
      qa_q1_actor_field_get(entity, "teleport.ignoreTargetname") == NULL)
    return;
  const bool player = qa_q1_is_player(game, &other);
  if ((flags_of(entity) & 1) != 0 && !player) return;
  if (qa_q1_health(game, other) <= 0 || (!player && !qa_q1_get_source_target(game, other).slidebox)) return;
  qa_q1_actor *target = NULL;
  if (entity->target == NULL || qa_q1_find(game, entity->target, &target, 1) == 0 || target == NULL)
    qa_range_error("could not find teleport target");
  qa_owned_actor mover;
  qa_q1_body body;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, other, &mover) || !body_of(game, other, &body)) return;
  qa_q1_use_targets(game, entity, &other);
  (void)qa_q1_spawn_teleport_fog(game, body.origin);
  if (player && !game->options.coop && game->options.deathmatch == 0 && game->source_fog_push.fn != NULL)
    (void)game->source_fog_push.fn(game->source_fog_push.ctx, other, target);
  const qa_vec3 destination = qa_q1_get_body(game, target).origin;
  const qa_vec3 forward = qa_q1_make_vectors(game, target->mangle).forward;
  (void)qa_q1_spawn_teleport_fog(game, qa_q1_vadd(destination, qa_q1_vscale(forward, 32)));
  (void)qa_q1_spawn_teledeath(game, destination, other);
  qa_q1_body patch = body;
  patch.origin = destination;
  patch.angles = target->mangle;
  patch.velocity = player ? qa_q1_vscale(forward, 300) : body.velocity;
  patch.has_ground = false;
  qa_shared_body_table_write(game->host.bodies, mover, qa_q1_body_to_state(&patch));
  qa_shared_body_table_link(game->host.bodies, mover, NULL);
  if (player) {
    qa_q1_player_state *state = qa_q1_player(game, other);
    if (state != NULL) state->teleport_until = game->time + 0.7;
    qa_q1_event event = {0};
    event.kind = "teleport-player";
    event.has_player = true;
    event.player = other;
    event.angles = target->mangle;
    event.lock_until = game->time + 0.7;
    qa_q1_emit(game, event);
  }
}

static void changelevel_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                              const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  const char *map = qa_q1_actor_text(entity, "map");
  if (!qa_q1_is_player(game, &other)) return;
  const double no_exit = game->options.has_no_exit ? game->options.no_exit : 0;
  if (no_exit == 1 || (no_exit == 2 && (game->map_name == NULL || strcmp(game->map_name, "start") != 0))) {
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 50000, NULL, "direct", "exit", NULL);
    return;
  }
  qa_q1_use_targets(game, entity, &other);
  memset(&entity->touch, 0, sizeof entity->touch);
  if ((flags_of(entity) & 1) != 0 && game->options.deathmatch == 0) {
    qa_q1_travel(game, map, &other);
    return;
  }
  set_activator(entity, &other);
  qa_q1_schedule(game, entity, 0.1, named_action(game, entity, "execute_changelevel"));
}

typedef struct next_path {
  const char *name;
  bool has_target;
  qa_actor_id target;
  double pause_until;
} next_path;
static next_path next_path_target(qa_q1_services *game, qa_q1_actor *corner) {
  next_path result;
  result.has_target = qa_q1_path_goal(game, corner->target != NULL ? corner->target : "", &result.target);
  result.name = result.has_target ? corner->target : "";
  result.pause_until = result.has_target ? 0 : qa_q1_path_end_time(game->time);
  return result;
}

static void path_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_actor *actor = qa_q1_get_entity(game, &other);
  if (actor != NULL && qa_q1_source_path_touch(game, entity, actor)) return;
  qa_q1_authored_path_follower follower;
  if (game->authored_path_follower.fn != NULL &&
      game->authored_path_follower.fn(game->authored_path_follower.ctx, other, &follower)) {
    if (follower.targetname == NULL || entity->targetname == NULL ||
        strcmp(follower.targetname, entity->targetname) != 0 || follower.has_enemy)
      return;
    const next_path next = next_path_target(game, entity);
    follower.advance(follower.ctx, next.name, next.has_target ? &next.target : NULL, next.pause_until);
    return;
  }
  if (actor == NULL || actor->monster == NULL) return;
  qa_q1_monster *monster = actor->monster;
  if (monster->path == NULL || entity->targetname == NULL || strcmp(monster->path, entity->targetname) != 0 ||
      monster->has_enemy)
    return;
  const next_path next = next_path_target(game, entity);
  set_string(&monster->path, next.name);
  if (!next.has_target) {
    monster->pause_until = next.pause_until;
    qa_q1_call_action(game, actor, actor->path_end);
  } else {
    qa_q1_body target_body;
    qa_vec3 a = body_of(game, next.target, &target_body) ? target_body.origin : QA_Q1_ZERO;
    qa_vec3 b = qa_q1_get_body(game, actor).origin;
    actor->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(a, b));
  }
}

static void activate_coop_spawns(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  if (!qa_q1_live(game, entity)) return;
  for (size_t i = 0; i < game->entity_count; i++)
    if (game->entities[i] != NULL && game->entities[i]->classname != NULL &&
        !strcmp(game->entities[i]->classname, "info_player_coop"))
      game->entities[i]->activated = true;
  qa_q1_use_targets(game, entity, activator);
  if (qa_q1_live(game, entity)) {
    memset(&entity->touch, 0, sizeof entity->touch);
    qa_q1_schedule(game, entity, 0.1, named_action(game, entity, "SUB_Remove"));
  }
}

static void speaker_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o; (void)a;
  const char *noise = qa_q1_actor_text(entity, "noise");
  if (strcmp(noise, "") != 0) qa_q1_sound_defaults(game, entity->actor.id, noise);
}
static void timer_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  double delay;
  qa_q1_bound_action callback;
  (void)ctx; (void)o; (void)a;
  entity->activated = !entity->activated;
  if (entity->activated)
    (delay = timer_interval(game, entity), callback = named_action(game, entity, "timer_fire"), qa_q1_schedule(game, entity, delay, callback));
  else
    qa_q1_cancel(game, entity);
}
static void timer_fire(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  double delay;
  qa_q1_bound_action callback;
  (void)ctx;
  if (!entity->activated) return;
  qa_q1_use_targets(game, entity, &entity->actor.id);
  if (qa_q1_live(game, entity) && entity->activated)
    (delay = timer_interval(game, entity), callback = named_action(game, entity, "timer_fire"), qa_q1_schedule(game, entity, delay, callback));
}
static void delay_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o;
  set_activator(entity, a);
  qa_q1_schedule(game, entity, qa_js_max(0, entity->wait), named_action(game, entity, "delay_fire"));
}
static void delay_fire(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, activator_of(entity));
}
static void remove_powerups_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o,
                                const qa_actor_id *activator) {
  (void)ctx; (void)entity; (void)o;
  if (activator == NULL) return;
  qa_q1_player_state *player = qa_q1_player(game, *activator);
  if (player == NULL) return;
  const bool flight = qa_q1_player_powerup_get(player, "q3:flight") > game->time;
  qa_q1_powerup kinds[16];
  const size_t count = player->powerup_count < 16 ? player->powerup_count : 16;
  for (size_t i = 0; i < count; i++) kinds[i] = player->powerups[i].kind;
  for (size_t i = 0; i < count; i++) {
    qa_q1_player_powerup_delete(player, kinds[i]);
    if (game->host.powerup) game->host.powerup(game->host.ctx, player->actor, kinds[i], 0);
  }
  if (flight && game->host.set_gravity) qa_q1_set_gravity(game, *activator, 1);
}
static void always_fire(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, NULL);
  if (qa_q1_live(game, entity)) qa_q1_remove(game, entity);
}
static void static_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o; (void)a;
  if (entity->solid && !strcmp(entity->solid, "bsp")) {
    entity->solid = "none";
    set_string(&entity->model, "");
  } else {
    entity->solid = "bsp";
    set_string(&entity->model, entity->original_model ? entity->original_model : "");
  }
  qa_q1_link(game, entity);
}
static void bob_top(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "top";
  const double wait = qa_q1_actor_number(entity, "wait");
  if (wait > 0) qa_q1_schedule(game, entity, wait, named_action(game, entity, "bob_down"));
  else qa_q1_calc_move(game, entity, entity->pos1, entity->speed, named_action(game, entity, "bob_bottom"));
}
static void bob_down(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_calc_move(game, entity, entity->pos1, entity->speed, named_action(game, entity, "bob_bottom"));
}
static void bob_bottom(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "bottom";
  const double wait = qa_q1_actor_number(entity, "wait");
  if (wait > 0) qa_q1_schedule(game, entity, wait, named_action(game, entity, "bob_up"));
  else qa_q1_calc_move(game, entity, entity->pos2, entity->speed, named_action(game, entity, "bob_top"));
}
static void bob_up(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_calc_move(game, entity, entity->pos2, entity->speed, named_action(game, entity, "bob_top"));
}
static void q3_push_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *n, const qa_touch_surface *s) {
  (void)ctx; (void)n; (void)s;
  qa_q1_actor *other_entity = qa_q1_get_entity(game, &other);
  if (qa_q1_health(game, other) <= 0 &&
      (other_entity == NULL || other_entity->classname == NULL || strcmp(other_entity->classname, "grenade") != 0))
    return;
  qa_owned_actor actor;
  qa_q1_body body;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, other, &actor) || !body_of(game, other, &body)) return;
  if (!entity->activated) {
    qa_q1_actor *target = NULL;
    if (entity->target == NULL || qa_q1_find(game, entity->target, &target, 1) == 0 || target == NULL) return;
    const qa_q1_body source = qa_q1_get_body(game, entity);
    const qa_vec3 center = qa_q1_vscale(
        qa_q1_vadd(qa_q1_vadd(source.origin, source.bounds.min), qa_q1_vadd(source.origin, source.bounds.max)), 0.5);
    const qa_vec3 dest = qa_q1_get_body(game, target).origin;
    const double height = dest.z - center.z, gravity = qa_q1_option_gravity(game);
    if (height <= 0 || gravity <= 0) return;
    const double time = sqrt(height / (0.5 * gravity));
    const double dx = dest.x - center.x, dy = dest.y - center.y, distance = qa_js_hypot2(dx, dy);
    entity->pos1 = v3(distance == 0 ? 0 : dx / time, distance == 0 ? 0 : dy / time, time * gravity);
    entity->activated = true;
  }
  qa_q1_body patch = body;
  patch.velocity = entity->pos1;
  patch.has_ground = false;
  qa_shared_body_table_write(game->host.bodies, actor, qa_q1_body_to_state(&patch));
  qa_shared_body_table_link(game->host.bodies, actor, NULL);
}
static void coop_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o;
  activate_coop_spawns(game, entity, a);
}
static void coop_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *n, const qa_touch_surface *s) {
  (void)ctx; (void)n; (void)s;
  if (qa_q1_is_player(game, &other)) activate_coop_spawns(game, entity, &other);
}
static void multi_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o;
  multi_fire(game, entity, a);
}
static void multi_killed(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  multi_fire(game, entity, attacker);
}
static void multi_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *n, const qa_touch_surface *s) {
  (void)ctx; (void)n; (void)s;
  if (!qa_q1_is_player(game, &other)) return;
  qa_q1_body body;
  if (!body_of(game, other, &body)) return;
  if (qa_q1_dot(qa_q1_make_vectors(game, body.angles).forward, entity->movedir) < 0) return;
  multi_fire(game, entity, &other);
}
static void multi_wait(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (entity->max_health > 0) {
    qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
    qa_q1_actor_set_damageable(entity, true);
    entity->solid = "bbox";
  }
}
static void counter_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o,
                        const qa_actor_id *activator) {
  (void)ctx; (void)o;
  entity->count -= 1;
  if (entity->count < 0) return;
  if ((flags_of(entity) & 1) == 0)
    qa_q1_message(game, activator,
                  entity->count >= 4 ? "$qc_more_go"
                  : entity->count == 3 ? "$qc_three_more"
                  : entity->count == 2 ? "$qc_two_more"
                  : entity->count == 1 ? "$qc_one_more" : "$qc_sequence_completed",
                  true, NULL, 0);
  if (entity->count != 0) return;
  qa_q1_use_targets(game, entity, activator);
  qa_q1_schedule(game, entity, 0.1, named_action(game, entity, "SUB_Remove"));
}
static void teleport_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o; (void)a;
  game->force_retouch = 2;
  qa_q1_schedule(game, entity, 0.2, named_action(game, entity, "SUB_Null"));
}
static void play_teleport(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const double index = qa_js_min(4, floor(fr(qa_q1_host_random(game) * 5)));
  char path[32];
  (void)snprintf(path, sizeof path, "misc/r_tele%d.wav", (int)index + 1);
  qa_q1_sound_defaults(game, entity->actor.id, path);
  qa_q1_remove(game, entity);
}
static void tdeath_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id victim,
                         const qa_vec3 *n, const qa_touch_surface *s) {
  (void)ctx; (void)n; (void)s;
  if (!entity->has_owner || qa_same_actor(victim, entity->owner)) return;
  const qa_actor_id owner = entity->owner;
  if (qa_q1_is_player(game, &victim)) {
    qa_q1_player_state *player = qa_q1_player(game, victim);
    if (player != NULL && qa_q1_player_powerup_get(player, "invulnerability") > game->time)
      set_string(&entity->classname, "teledeath2");
    if (!qa_q1_is_player(game, &owner)) {
      (void)qa_q1_damage(game, owner, entity->actor.id, &entity->actor.id, 50000, NULL, "direct", entity->classname, NULL);
      return;
    }
  }
  if (qa_q1_health(game, victim) != 0)
    (void)qa_q1_damage(game, victim, entity->actor.id, &entity->actor.id, 50000, NULL, "direct", entity->classname, NULL);
}
static void light_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o; (void)a;
  entity->spawnflags = (double)(qa_js_to_int32(entity->spawnflags) ^ 1);
  double style2 = qa_q1_actor_number(entity, "style");
  const char * pattern = (flags_of(entity) & 1) != 0 ? "a" : "m";
  emit_lightstyle(game, style2, pattern);
}
static void barrel_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  set_string(&entity->classname, "explo_box");
  qa_q1_actor_set_damageable(entity, false);
  set_activator(entity, attacker);
  qa_q1_schedule(game, entity, 0.3, named_action(game, entity, "barrel_explode"));
}
static void barrel_explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_radius_damage(game, entity->actor.id, activator_of(entity), 160, NULL, NULL, "");
  qa_q1_sound_defaults(game, entity->actor.id, "weapons/r_exp3.wav");
  qa_q1_effect(game, "explosion", qa_q1_vadd(qa_q1_get_body(game, entity).origin, v3(0, 0, 32)), NULL, 1);
  qa_q1_remove(game, entity);
}
static void func_wall_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)game; (void)o; (void)a;
  entity->frame = 1 - entity->frame;
}
static void relay_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *o, const qa_actor_id *a) {
  (void)ctx; (void)o;
  qa_q1_use_targets(game, entity, a);
}
static void execute_changelevel(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_begin_intermission(game, qa_q1_actor_text(entity, "map"), activator_of(entity));
}
static void hurt_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *n, const qa_touch_surface *s) {
  (void)ctx; (void)n; (void)s;
  if (entity->solid == NULL || strcmp(entity->solid, "trigger") != 0 || !can_take_damage(game, other)) return;
  entity->solid = "none";
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "trigger", NULL);
  qa_q1_schedule(game, entity, 1, named_action(game, entity, "hurt_on"));
}
static void hurt_on(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->solid = "trigger";
  qa_q1_link(game, entity);
}
static void push_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *n, const qa_touch_surface *s) {
  (void)ctx; (void)n; (void)s;
  qa_owned_actor actor;
  qa_q1_body body;
  const bool owned = qa_session_actor_registry_resolve_owned(game->host.actors, other, &actor);
  const bool bodied = body_of(game, other, &body);
  if (owned && bodied) {
    const qa_q1_actor *other_entity = qa_q1_get_entity(game, &other);
    if (qa_q1_health(game, other) > 0 ||
        (other_entity != NULL && other_entity->classname != NULL && !strcmp(other_entity->classname, "grenade"))) {
      qa_q1_body patch = body;
      patch.velocity = qa_q1_vscale(entity->movedir, entity->speed * 10);
      qa_shared_body_table_write(game->host.bodies, actor, qa_q1_body_to_state(&patch));
      /* push_touch's per-player fly_sound limiter (not saved). */
      qa_q1_player_state *player = qa_q1_player(game, other);
      if (player != NULL &&
          qa_q1_player_timer_get(game->wind_fly_sounds, game->wind_fly_sound_count, player) < game->time) {
        qa_q1_player_timer_set(&game->wind_fly_sounds, &game->wind_fly_sound_count, &game->wind_fly_sound_capacity,
                               player, game->time + 1.5);
        qa_q1_sound_defaults(game, actor.id, "ambience/windfly.wav");
      }
    }
  }
  if ((flags_of(entity) & 1) != 0) qa_q1_remove(game, entity);
}

static void reg(qa_q1_services *game, const char *name, qa_q1_callback_handlers h) {
  qa_q1_callback_registry_register(game->named, name, h);
}

void qa_q1_register_spawn_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers h = {0};
  h.use = speaker_use; reg(game, "speaker_use", h);
  h = (qa_q1_callback_handlers){0}; h.use = timer_use; reg(game, "timer_use", h);
  h = (qa_q1_callback_handlers){0}; h.action = timer_fire; reg(game, "timer_fire", h);
  h = (qa_q1_callback_handlers){0}; h.use = delay_use; reg(game, "delay_use", h);
  h = (qa_q1_callback_handlers){0}; h.action = delay_fire; reg(game, "delay_fire", h);
  h = (qa_q1_callback_handlers){0}; h.use = remove_powerups_use; reg(game, "remove_powerups_use", h);
  h = (qa_q1_callback_handlers){0}; h.action = always_fire; reg(game, "always_fire", h);
  h = (qa_q1_callback_handlers){0}; h.use = static_use; reg(game, "static_use", h);
  h = (qa_q1_callback_handlers){0}; h.action = bob_top; reg(game, "bob_top", h);
  h = (qa_q1_callback_handlers){0}; h.action = bob_down; reg(game, "bob_down", h);
  h = (qa_q1_callback_handlers){0}; h.action = bob_bottom; reg(game, "bob_bottom", h);
  h = (qa_q1_callback_handlers){0}; h.action = bob_up; reg(game, "bob_up", h);
  h = (qa_q1_callback_handlers){0}; h.touch = q3_push_touch; reg(game, "q3_push_touch", h);
  h = (qa_q1_callback_handlers){0}; h.use = coop_use; reg(game, "activate_coop_spawns_use", h);
  h = (qa_q1_callback_handlers){0}; h.touch = coop_touch; reg(game, "activate_coop_spawns_touch", h);
  h = (qa_q1_callback_handlers){0}; h.use = multi_use; reg(game, "multi_use", h);
  h = (qa_q1_callback_handlers){0}; h.die = multi_killed; reg(game, "multi_killed", h);
  h = (qa_q1_callback_handlers){0}; h.touch = multi_touch; reg(game, "multi_touch", h);
  h = (qa_q1_callback_handlers){0}; h.action = multi_wait; reg(game, "multi_wait", h);
  h = (qa_q1_callback_handlers){0}; h.use = counter_use; reg(game, "counter_use", h);
  h = (qa_q1_callback_handlers){0}; h.use = teleport_use; reg(game, "teleport_use", h);
  h = (qa_q1_callback_handlers){0}; h.touch = teleport_touch; reg(game, "teleport_touch", h);
  h = (qa_q1_callback_handlers){0}; h.action = play_teleport; reg(game, "play_teleport", h);
  h = (qa_q1_callback_handlers){0}; h.touch = tdeath_touch; reg(game, "tdeath_touch", h);
  h = (qa_q1_callback_handlers){0}; h.use = light_use; reg(game, "light_use", h);
  h = (qa_q1_callback_handlers){0}; h.die = barrel_die; reg(game, "barrel_die", h);
  h = (qa_q1_callback_handlers){0}; h.action = barrel_explode; reg(game, "barrel_explode", h);
  h = (qa_q1_callback_handlers){0}; h.use = func_wall_use; reg(game, "func_wall_use", h);
  h = (qa_q1_callback_handlers){0}; h.use = relay_use; reg(game, "trigger_relay_use", h);
  h = (qa_q1_callback_handlers){0}; h.touch = changelevel_touch; reg(game, "changelevel_touch", h);
  h = (qa_q1_callback_handlers){0}; h.action = execute_changelevel; reg(game, "execute_changelevel", h);
  h = (qa_q1_callback_handlers){0}; h.touch = hurt_touch; reg(game, "hurt_touch", h);
  h = (qa_q1_callback_handlers){0}; h.action = hurt_on; reg(game, "hurt_on", h);
  h = (qa_q1_callback_handlers){0}; h.touch = push_touch; reg(game, "push_touch", h);
  h = (qa_q1_callback_handlers){0}; h.touch = path_touch; reg(game, "movetarget_touch", h);
}
