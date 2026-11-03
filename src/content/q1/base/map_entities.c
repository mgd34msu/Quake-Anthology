/* Port of src/content/q1/base/map-entities.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/map_entities.h"

#include "base_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/rules.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
#define FR(x) ((double)(float)(x))
static const qa_vec3 ZERO3 = {0, 0, 0};

const char *const QA_Q1_REMAINING_MAP_CLASSNAMES[] = {
  "func_train", "misc_teleporttrain", "func_illusionary", "func_episodegate", "func_bossgate",
  "item_sigil", "event_lightning", "testplayerstart", "trigger_changelevel",
  "trigger_setskill", "trigger_onlyregistered", "trigger_monsterjump", "trap_spikeshooter",
  "trap_shooter", "misc_fireball", "air_bubbles",
  "light_globe", "light_torch_small_walltorch", "light_flame_large_yellow",
  "light_flame_small_yellow", "light_flame_small_white",
  "ambient_suck_wind", "ambient_flouro_buzz", "ambient_drip", "ambient_thunder",
  "ambient_light_buzz", "ambient_swamp1", "ambient_swamp2", "viewthing", "misc_noisemaker"
};
const size_t QA_Q1_REMAINING_MAP_CLASSNAME_COUNT =
    sizeof QA_Q1_REMAINING_MAP_CLASSNAMES / sizeof *QA_Q1_REMAINING_MAP_CLASSNAMES;

static void set_angles_zero(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = ZERO3;
  qa_q1_set_body(game, entity, &patch);
}
static void init_trigger(qa_q1_services *game, qa_q1_actor *entity) {
  entity->movedir = qa_q1_move_direction(qa_q1_get_body(game, entity).angles, game);
  entity->solid = "trigger";
  entity->model[0] = '\0';
  set_angles_zero(game, entity);
}
static void later(qa_q1_services *game, qa_q1_actor *entity, double delay, const char *name) {
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}
void qa_q1_make_static(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_event event = {0};
  event.kind = "static-model";
  event.has_origin = true;
  event.path = entity->model;
  event.frame = trunc(entity->frame);
  event.color_map = trunc(qa_q1_actor_number(entity, "colormap"));
  event.skin = trunc(entity->skin);
  event.origin = body.origin;
  event.angles = body.angles;
  qa_q1_emit(game, event);
  qa_q1_remove(game, entity);
}
static qa_q1_actor *find_first(qa_q1_services *game, const char *target) {
  qa_q1_actor *found[1];
  return qa_q1_find(game, target, found, 1) > 0 ? found[0] : NULL;
}
static void train_next(qa_q1_services *game, qa_q1_actor *entity) {
  entity->activated = true;
  qa_q1_actor *corner = find_first(game, entity->target);
  if (corner == NULL) qa_range_error("Train target not found: %s", entity->target);
  q1base_set_text(&entity->target, corner->target);
  if (entity->target[0] == '\0') qa_range_error("train_next: no next target");
  entity->wait = corner->wait;
  qa_q1_sound_defaults(game, entity->actor.id, entity->sounds == 1 ? "plats/train1.wav" : "misc/null.wav");
  qa_vec3 a = qa_q1_get_body(game, corner).origin;
  qa_vec3 b = qa_q1_get_body(game, entity).bounds.min;
  qa_vec3 destination = qa_sub3(a, b);
  qa_q1_calc_move(game, entity, destination, entity->speed,
                  qa_q1_callback_registry_action(game->named, entity, "base:train_wait"));
}
static void train_spawn(qa_q1_services *game, qa_q1_actor *entity) {
  bool teleport = strcmp(entity->classname, "misc_teleporttrain") == 0;
  if (entity->target[0] == '\0') qa_range_error("%s without a target", entity->classname);
  const char *sounds[2] = {NULL, NULL};
  size_t sound_count = 0;
  if (teleport || entity->sounds == 0) { sounds[0] = "misc/null.wav"; sounds[1] = "misc/null.wav"; sound_count = 2; }
  else if (entity->sounds == 1) { sounds[0] = "plats/train2.wav"; sounds[1] = "plats/train1.wav"; sound_count = 2; }
  if (qa_q1_services_uses_id1(game))
    for (size_t i = 0; i < sound_count; i++) qa_q1_services_precache_sound(game, sounds[i]);
  if (qa_q1_services_uses_id1(game) && teleport) qa_q1_services_precache_model(game, "progs/teleport.mdl");
  /* TS `||=`: 0 and NaN take the default. */
  if (entity->speed == 0 || isnan(entity->speed)) entity->speed = 100;
  if (entity->damage == 0 || isnan(entity->damage)) entity->damage = 2;
  entity->movement = "push";
  entity->solid = teleport ? "none" : "bsp";
  if (teleport) {
    q1base_set_text(&entity->model, "progs/teleport.mdl");
    entity->angular_velocity = V3(100, 200, 300);
  } else {
    q1base_set_text(&entity->classname, "train");
  }
  entity->state = "bottom";
  entity->activated = false;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "base:train_use");
  entity->blocked = qa_q1_callback_registry_blocked(game->named, entity, "base:train_blocked");
  double due_seconds = FR(qa_q1_actor_number(entity, "ltime") + 0.1);
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, entity, "base:train_find");
  qa_q1_schedule_at(game, entity, due_seconds, callback);
}
static void sigil_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (strcmp(entity->solid, "trigger") != 0 || !qa_q1_is_player(game, &other) ||
      qa_q1_health(game, other) <= 0) return;
  qa_q1_message(game, &other, strcmp(game->options.edition, "classic") == 0 ? "You got the rune!" : "$qc_got_rune", true, NULL, 0);
  qa_owned_actor player;
  if (qa_session_actor_registry_resolve_owned(game->host.actors, other, &player))
    qa_q1_sound(game, player.id, "misc/runekey.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &other, 1);
  entity->solid = "none";
  entity->model[0] = '\0';
  entity->touch = (qa_q1_bound_touch){0};
  qa_q1_link(game, entity);
  qa_q1_base *base = qa_q1_base_for(game);
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(base);
  int32_t flags = qa_js_to_int32(campaign.read_flags(campaign.ctx)); /* TS readFlags() | x: ToInt32 */
  campaign.write_flags(campaign.ctx, (double)(flags | (qa_js_to_int32(entity->spawnflags) & 15)));
  entity->classname[0] = '\0';
  qa_q1_use_targets(game, entity, &other);
}
static void sigil(qa_q1_services *game, qa_q1_actor *entity) {
  int bits = qa_js_to_int32(entity->spawnflags) & 15;
  if (bits == 0) qa_range_error("item_sigil has no episode spawnflags");
  if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_sound(game, "misc/runekey.wav");
  for (int episode = 1; episode <= 4; episode++) {
    if (qa_q1_services_uses_id1(game) && (bits & (1 << (episode - 1))) != 0) {
      char path[64];
      (void)snprintf(path, sizeof path, "progs/end%d.mdl", episode);
      qa_q1_services_precache_model(game, path);
    }
  }
  int number = (bits & 8) != 0 ? 4 : (bits & 4) != 0 ? 3 : (bits & 2) != 0 ? 2 : 1;
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/end%d.mdl", number); q1base_set_text(&entity->model, model_text); }
  entity->solid = "trigger";
  entity->movement = "toss";
  qa_q1_set_bounds(game, entity, (qa_bounds){V3(-16, -16, -24), V3(16, 16, 32)});
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "base:sigil_touch");
  later(game, entity, 0.2, "base:sigil_place");
}
static void shooter_fire_impl(qa_q1_services *game, qa_q1_actor *entity) {
  if ((qa_js_to_int32(entity->spawnflags) & 2) != 0) {
    qa_q1_sound_defaults(game, entity->actor.id, "enforcer/enfire.wav");
    qa_q1_actor *laser = qa_q1_launch_laser(game, &entity->actor.id, qa_q1_get_body(game, entity).origin, entity->movedir);
    if (strcmp(entity->classname, "trap_shooter") == 0) {
      qa_q1_body_patch patch = {0};
      patch.has_velocity = true; patch.velocity = qa_scale3(entity->movedir, 500);
      qa_q1_set_body(game, laser, &patch);
    }
  } else {
    qa_q1_sound_defaults(game, entity->actor.id, "weapons/spike2.wav");
    (void)qa_q1_launch_spike(game, &entity->actor.id, qa_q1_get_body(game, entity).origin, qa_scale3(entity->movedir, 500), (qa_js_to_int32(entity->spawnflags) & 1) != 0 ? "superspike" : "spike");
  }
}
static void shooter(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_actor_field_set(entity, "killstring", "$qc_ks_spiked");
  entity->movedir = qa_q1_move_direction(qa_q1_get_body(game, entity).angles, game);
  set_angles_zero(game, entity);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "base:shooter_fire");
  if ((qa_js_to_int32(entity->spawnflags) & 2) != 0) {
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_model(game, "progs/laser.mdl");
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_sound(game, "enforcer/enfire.wav");
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_sound(game, "enforcer/enfstop.wav");
  } else if (qa_q1_services_uses_id1(game)) {
    qa_q1_services_precache_sound(game, "weapons/spike2.wav");
  }
  if (strcmp(entity->classname, "trap_spikeshooter") == 0) return;
  if (entity->wait == 0) entity->wait = 1;
  later(game, entity, qa_q1_actor_number(entity, "nextthink") + entity->wait, "base:shooter_think");
}
qa_q1_actor *qa_q1_spawn_bubble(qa_q1_services *game, qa_vec3 origin, const qa_vec3 *velocity, bool split) {
  qa_q1_actor *bubble = qa_q1_create(game, "bubble", NULL, NULL);
  q1base_set_text(&bubble->model, "progs/s_bubble.spr");
  bubble->movement = "noclip";
  bubble->frame = split ? 1 : 0;
  bubble->count = split ? 10 : 0;
  qa_vec3 velocity_value = velocity != NULL ? *velocity : V3(0, 0, 15);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity_value;
  patch.has_bounds = true; patch.bounds = (qa_bounds){V3(-8, -8, -8), V3(8, 8, 8)};
  qa_q1_set_body(game, bubble, &patch);
  qa_q1_link(game, bubble);
  later(game, bubble, 0.5, "base:bubble_bob");
  return bubble;
}
static void bubble_bob(void *ctx, qa_q1_services *game, qa_q1_actor *bubble) {
  (void)ctx;
  qa_q1_body body = qa_q1_get_body(game, bubble);
  bubble->count++;
  /* bubble_split removes the parent because a noclip bubble's waterlevel is never 3;
   * nothing else ends a bubble before its 20th step. */
  if (bubble->count == 4) {
    (void)qa_q1_spawn_bubble(game, body.origin, &body.velocity, true);
    qa_q1_remove(game, bubble);
    return;
  }
  if (bubble->count >= 20) { qa_q1_remove(game, bubble); return; }
  double x = body.velocity.x - 10 + game->host.random(game->host.ctx) * 20;
  double y = body.velocity.y - 10 + game->host.random(game->host.ctx) * 20;
  double z = body.velocity.z + 10 + game->host.random(game->host.ctx) * 10;
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = V3(x > 10 ? 5 : x < -10 ? -5 : x, y > 10 ? 5 : y < -10 ? -5 : y,
                      z > 30 ? 25 : z < 10 ? 15 : z);
  qa_q1_set_body(game, bubble, &patch);
  later(game, bubble, 0.5, "base:bubble_bob");
}
static void changelevel_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                              const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  double no_exit = game->options.has_no_exit ? game->options.no_exit : 0;
  if (no_exit == 1 || (no_exit == 2 && strcmp(game->map_name, "start") != 0)) {
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 50000, NULL, "direct", "exit", NULL);
    return;
  }
  qa_q1_base *base = qa_q1_base_for(game);
  const char *map = qa_q1_actor_text(entity, "map");
  qa_q1_level_rules_changelevel_touched(qa_q1_base_level_rules(base), entity, other);
  const qa_q1_base_options *options = qa_q1_base_options_of(base);
  if (options->player_exited != NULL) options->player_exited(options->ctx, other);
  qa_q1_use_targets(game, entity, &other);
  if ((qa_js_to_int32(entity->spawnflags) & 1) != 0 && game->options.deathmatch == 0) {
    bool same = options->same_level != NULL && options->same_level(options->ctx);
    qa_q1_level_rules_travel_to(qa_q1_base_level_rules(base), same ? game->map_name : map,
                                &other);
    return;
  }
  entity->touch = (qa_q1_bound_touch){0};
  entity->has_activator = true; entity->activator = other;
  later(game, entity, 0.1, "base:execute_changelevel");
}
static void fireball_fly(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const qa_actor_id * owner = &entity->actor.id;
  qa_vec3 origin2 = qa_q1_get_body(game, entity).origin;
  /* TS object literal: x, y, z random draws in order. */
  qa_vec3 velocity;
  velocity.x = game->host.random(game->host.ctx) * 100 - 50;
  velocity.y = game->host.random(game->host.ctx) * 100 - 50;
  velocity.z = entity->speed + game->host.random(game->host.ctx) * 200;
  qa_q1_actor *missile = qa_q1_create_missile(game, owner, "fireball", "lavaball", origin2, velocity);
  missile->solid = "trigger";
  missile->movement = "toss";
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:fireball_touch");
  later(game, entity, game->host.random(game->host.ctx) * 5 + 3, "base:fireball_fly");
}

static void train_next_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  train_next(game, entity);
}
static void train_wait(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (entity->wait != 0)
    qa_q1_sound_defaults(game, entity->actor.id, entity->sounds == 1 ? "plats/train2.wav" : "misc/null.wav");
  double due_seconds = FR(qa_q1_actor_number(entity, "ltime") + (entity->wait != 0 ? entity->wait : 0.1));
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, entity, "base:train_next");
  qa_q1_schedule_at(game, entity, due_seconds, callback);
}
static void train_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  if (strcmp(entity->state, "top") == 0 && !entity->activated) train_next(game, entity);
}
static void train_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx;
  if (game->time < entity->attack_finished) return;
  entity->attack_finished = game->time + 0.5;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "crush", NULL);
}
static void train_find(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  double due_seconds;
  qa_q1_bound_action callback;
  (void)ctx;
  qa_q1_actor *corner = find_first(game, entity->target);
  if (corner == NULL) qa_range_error("Train first target not found: %s", entity->target);
  q1base_set_text(&entity->target, corner->target);
  qa_vec3 a = qa_q1_get_body(game, corner).origin;
  qa_vec3 b = qa_q1_get_body(game, entity).bounds.min;
  qa_q1_set_origin(game, entity, qa_sub3(a, b));
  entity->state = "top";
  if (entity->targetname[0] == '\0')
    (due_seconds = FR(qa_q1_actor_number(entity, "ltime") + 0.1), callback = qa_q1_callback_registry_action(game->named, entity, "base:train_next"), qa_q1_schedule_at(game, entity, due_seconds,
                      callback));
}
static void sigil_place(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 start = qa_add3(body.origin, V3(0, 0, 6));
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = qa_add3(start, V3(0, 0, -256));
  request.bounds = body.bounds;
  request.has_ignore = true;
  request.ignore = entity->actor.id;
  request.monsters = true;
  qa_q1_trace floor_trace = qa_q1_host_trace(game, request);
  if (floor_trace.all_solid || floor_trace.fraction == 1) { qa_q1_remove(game, entity); return; }
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = floor_trace.end;
  patch.has_velocity = true; patch.velocity = ZERO3;
  patch.has_ground = true; patch.ground = floor_trace.has_actor ? &floor_trace.actor : NULL;
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
}
static void shooter_fire_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity,
                             const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  shooter_fire_impl(game, entity);
}
static void shooter_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  shooter_fire_impl(game, entity);
  later(game, entity, entity->wait, "base:shooter_think");
}
static void execute_changelevel(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_level_rules_begin(qa_q1_base_level_rules(qa_q1_base_for(game)),
                          qa_q1_actor_text(entity, "map"), entity->has_activator ? &entity->activator : NULL);
}
static void wall_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  entity->frame = 1 - entity->frame;
}
static void setskill_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  double parsed = qa_js_string_to_number(entity->message); /* Number(entity.message) */
  double value = floor(parsed);
  if (value < 0) value = 0;
  if (value > 3) value = 3;
  if (value == 0 || value == 1 || value == 2 || value == 3) {
    qa_q1_campaign_binding campaign = qa_q1_base_campaign(qa_q1_base_for(game));
    campaign.set_skill(campaign.ctx, (int)value);
  }
}
static void registered_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                             const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other) || entity->attack_finished > game->time) return;
  entity->attack_finished = game->time + 2;
  if (qa_q1_base_registered(qa_q1_base_for(game))) {
    entity->message[0] = '\0';
    qa_q1_use_targets(game, entity, &other);
    qa_q1_remove(game, entity);
    return;
  }
  if (entity->message[0] != '\0') {
    qa_q1_message(game, &other, entity->message, true, NULL, 0);
    qa_owned_actor player;
    if (qa_session_actor_registry_resolve_owned(game->host.actors, other, &player))
      qa_q1_sound(game, player.id, "misc/talk.wav", "body", 1, 1);
  }
}
static void monsterjump_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                              const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_actor *monster = qa_q1_get_entity(game, &other);
  qa_q1_body body;
  bool have_body = q1base_read_body(game, other, &body);
  if (monster == NULL || monster->monster == NULL || !have_body || (qa_js_to_int32(monster->movement_flags) & 3) != 0) return;
  double height = qa_q1_actor_number(entity, "height");
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = V3(entity->movedir.x * entity->speed, entity->movedir.y * entity->speed,
                      !body.has_ground ? body.velocity.z : height != 0 ? height : 200);
  patch.has_ground = true; patch.ground = NULL;
  qa_q1_set_body(game, monster, &patch);
  if (body.has_ground) monster->movement_flags = qa_js_to_int32(monster->movement_flags) & (~512);
}
static void fireball_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 20, NULL, "direct", "", NULL);
  qa_q1_remove(game, entity);
}
static void make_bubbles(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  (void)qa_q1_spawn_bubble(game, qa_q1_get_body(game, entity).origin, NULL, false);
  later(game, entity, game->host.random(game->host.ctx) + 0.5, "base:make_bubbles");
}
static void noisemaker(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  typedef struct noise { const char *path; qa_q1_sound_channel channel; } noise;
  const noise sounds[] = {
    {"enfire", "weapon"}, {"enfstop", "voice"}, {"sight1", "item"},
    {"sight2", "body"}, {"sight3", "5"}, {"sight4", "6"}, {"pain1", "7"}
  };
  for (size_t i = 0; i < sizeof sounds / sizeof *sounds; i++) {
    char path[64];
    (void)snprintf(path, sizeof path, "enforcer/%s.wav", sounds[i].path);
    qa_q1_sound(game, entity->actor.id, path, sounds[i].channel, 1, 1);
  }
  later(game, entity, 0.5, "base:noisemaker");
}
void qa_q1_register_map_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers handlers;
  handlers = (qa_q1_callback_handlers){.action = train_next_action};
  qa_q1_callback_registry_register(game->named, "base:train_next", handlers);
  handlers = (qa_q1_callback_handlers){.action = train_wait};
  qa_q1_callback_registry_register(game->named, "base:train_wait", handlers);
  handlers = (qa_q1_callback_handlers){.use = train_use};
  qa_q1_callback_registry_register(game->named, "base:train_use", handlers);
  handlers = (qa_q1_callback_handlers){.blocked = train_blocked};
  qa_q1_callback_registry_register(game->named, "base:train_blocked", handlers);
  handlers = (qa_q1_callback_handlers){.action = train_find};
  qa_q1_callback_registry_register(game->named, "base:train_find", handlers);
  handlers = (qa_q1_callback_handlers){.touch = sigil_touch};
  qa_q1_callback_registry_register(game->named, "base:sigil_touch", handlers);
  handlers = (qa_q1_callback_handlers){.action = sigil_place};
  qa_q1_callback_registry_register(game->named, "base:sigil_place", handlers);
  handlers = (qa_q1_callback_handlers){.use = shooter_fire_use};
  qa_q1_callback_registry_register(game->named, "base:shooter_fire", handlers);
  handlers = (qa_q1_callback_handlers){.action = shooter_think};
  qa_q1_callback_registry_register(game->named, "base:shooter_think", handlers);
  handlers = (qa_q1_callback_handlers){.action = bubble_bob};
  qa_q1_callback_registry_register(game->named, "base:bubble_bob", handlers);
  handlers = (qa_q1_callback_handlers){.touch = changelevel_touch};
  qa_q1_callback_registry_register(game->named, "base:changelevel_touch", handlers);
  handlers = (qa_q1_callback_handlers){.action = execute_changelevel};
  qa_q1_callback_registry_register(game->named, "base:execute_changelevel", handlers);
  handlers = (qa_q1_callback_handlers){.use = wall_use};
  qa_q1_callback_registry_register(game->named, "base:wall_use", handlers);
  handlers = (qa_q1_callback_handlers){.touch = setskill_touch};
  qa_q1_callback_registry_register(game->named, "base:setskill_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = registered_touch};
  qa_q1_callback_registry_register(game->named, "base:registered_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = monsterjump_touch};
  qa_q1_callback_registry_register(game->named, "base:monsterjump_touch", handlers);
  handlers = (qa_q1_callback_handlers){.action = fireball_fly};
  qa_q1_callback_registry_register(game->named, "base:fireball_fly", handlers);
  handlers = (qa_q1_callback_handlers){.touch = fireball_touch};
  qa_q1_callback_registry_register(game->named, "base:fireball_touch", handlers);
  handlers = (qa_q1_callback_handlers){.action = make_bubbles};
  qa_q1_callback_registry_register(game->named, "base:make_bubbles", handlers);
  handlers = (qa_q1_callback_handlers){.action = noisemaker};
  qa_q1_callback_registry_register(game->named, "base:noisemaker", handlers);
}
static void emit_ambient(qa_q1_services *game, qa_q1_actor *entity, const char *path, double volume) {
  qa_q1_event event = {0};
  event.kind = "ambient";
  event.has_origin = true; /* TS ambient events always carry origin */
  event.origin = qa_q1_get_body(game, entity).origin;
  event.path = path;
  event.volume = volume;
  event.attenuation = 3;
  qa_q1_emit(game, event);
}
void qa_q1_spawn_remaining_map_actor(qa_q1_base *base, qa_q1_actor *entity) {
  qa_q1_services *game = qa_q1_base_game(base);
  const char *classname = entity->classname;
  if (strcmp(classname, "testplayerstart") == 0) return;
  if (strcmp(classname, "trigger_changelevel") == 0) {
    if (qa_q1_actor_text(entity, "map")[0] == '\0')
      qa_range_error("changelevel trigger doesn't have map");
    qa_q1_actor_field_set(entity, "killstring", "$qc_ks_tried_leave");
    init_trigger(game, entity);
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "base:changelevel_touch");
    return;
  }
  if (strcmp(classname, "func_train") == 0 || strcmp(classname, "misc_teleporttrain") == 0) {
    train_spawn(game, entity); return;
  }
  if (strcmp(classname, "item_sigil") == 0) { sigil(game, entity); return; }
  if (strcmp(classname, "event_lightning") == 0) { qa_q1_base_spawn_lightning(base, entity); return; }
  if (strcmp(classname, "func_episodegate") == 0 || strcmp(classname, "func_bossgate") == 0) {
    qa_q1_campaign_binding campaign = qa_q1_base_campaign(base);
    int32_t flags = qa_js_to_int32(campaign.read_flags(campaign.ctx)); /* TS readFlags() | x: ToInt32 */
    bool hide = strcmp(classname, "func_episodegate") == 0 ? (flags & qa_js_to_int32(entity->spawnflags)) == 0
                : (flags & 15) == 15;
    if (hide) { entity->model[0] = '\0'; return; }
    entity->solid = "bsp";
    entity->movement = "push";
    set_angles_zero(game, entity);
    entity->use = qa_q1_callback_registry_use(game->named, entity, "base:wall_use");
    return;
  }
  if (strcmp(classname, "func_illusionary") == 0) {
    set_angles_zero(game, entity);
    entity->solid = "none"; entity->movement = "none";
    qa_q1_make_static(game, entity);
    return;
  }
  if (strcmp(classname, "trigger_setskill") == 0) {
    init_trigger(game, entity);
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "base:setskill_touch");
    return;
  }
  if (strcmp(classname, "trigger_onlyregistered") == 0) {
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_sound(game, "misc/talk.wav");
    init_trigger(game, entity);
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "base:registered_touch");
    return;
  }
  if (strcmp(classname, "trigger_monsterjump") == 0) {
    if (qa_q1_get_body(game, entity).angles.y == 0) {
      qa_q1_body_patch patch = {0};
      patch.has_angles = true; patch.angles = V3(0, 360, 0);
      qa_q1_set_body(game, entity, &patch);
    }
    init_trigger(game, entity);
    if (entity->speed == 0 || isnan(entity->speed)) entity->speed = 200;
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, "base:monsterjump_touch");
    return;
  }
  if (strcmp(classname, "trap_spikeshooter") == 0 || strcmp(classname, "trap_shooter") == 0) {
    shooter(game, entity); return;
  }
  if (strcmp(classname, "misc_fireball") == 0) {
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_model(game, "progs/lavaball.mdl");
    q1base_set_text(&entity->classname, "fireball");
    qa_q1_actor_field_set(entity, "killstring", "$qc_ks_lavaball");
    /* id1 and QW misc.qc test `self.speed == 1000` (a no-op); only the
     * rerelease assigns the default. */
    if (strcmp(game->options.edition, "rerelease") == 0 && (entity->speed == 0 || isnan(entity->speed)))
      entity->speed = 1000;
    later(game, entity, game->host.random(game->host.ctx) * 5, "base:fireball_fly");
    return;
  }
  if (strcmp(classname, "air_bubbles") == 0) {
    if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_model(game, "progs/s_bubble.spr");
    later(game, entity, 1, "base:make_bubbles");
    return;
  }
  if (strcmp(classname, "light_globe") == 0) {
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_model(game, "progs/s_light.spr");
    q1base_set_text(&entity->model, "progs/s_light.spr");
    qa_q1_make_static(game, entity);
    return;
  }
  if (strcmp(classname, "light_torch_small_walltorch") == 0 ||
      strcmp(classname, "light_flame_large_yellow") == 0 ||
      strcmp(classname, "light_flame_small_yellow") == 0 ||
      strcmp(classname, "light_flame_small_white") == 0) {
    q1base_set_text(&entity->model, strcmp(classname, "light_torch_small_walltorch") == 0 ? "progs/flame.mdl" : "progs/flame2.mdl");
    entity->frame = strcmp(classname, "light_flame_large_yellow") == 0 ? 1 : 0;
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_model(game, entity->model);
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_sound(game, "ambience/fire1.wav");
    emit_ambient(game, entity, "ambience/fire1.wav", 0.5);
    qa_q1_make_static(game, entity);
    return;
  }
  {
    typedef struct ambient_name { const char *classname; const char *path; double volume; } ambient_name;
    static const ambient_name AMBIENTS[] = {
      {"ambient_suck_wind", "suck1", 1}, {"ambient_flouro_buzz", "buzz1", 1},
      {"ambient_drip", "drip1", 0.5}, {"ambient_thunder", "thunder1", 0.5},
      {"ambient_light_buzz", "fl_hum1", 0.5}, {"ambient_swamp1", "swamp1", 0.5},
      {"ambient_swamp2", "swamp2", 0.5}
    };
    for (size_t i = 0; i < sizeof AMBIENTS / sizeof *AMBIENTS; i++) {
      if (strcmp(classname, AMBIENTS[i].classname) == 0) {
        char path[64];
        (void)snprintf(path, sizeof path, "ambience/%s.wav", AMBIENTS[i].path);
        if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_sound(game, path);
        emit_ambient(game, entity, path, AMBIENTS[i].volume);
        return;
      }
    }
  }
  if (strcmp(classname, "viewthing") == 0) {
    if (qa_q1_services_uses_id1(game)) qa_q1_services_precache_model(game, "progs/player.mdl");
    q1base_set_text(&entity->model, "progs/player.mdl");
    return;
  }
  if (strcmp(classname, "misc_noisemaker") == 0) {
    static const char *const PATHS[] = {"enfire", "enfstop", "sight1", "sight2", "sight3",
                                        "sight4", "pain1", "pain2", "death1", "idle1"};
    for (size_t i = 0; i < 10; i++) {
      if (qa_q1_services_uses_id1(game)) {
        char path[64];
        (void)snprintf(path, sizeof path, "enforcer/%s.wav", PATHS[i]);
        qa_q1_services_precache_sound(game, path);
      }
    }
    later(game, entity, 0.1 + game->host.random(game->host.ctx), "base:noisemaker");
    return;
  }
  qa_range_error("Unknown base Q1 map class %s", classname);
}
