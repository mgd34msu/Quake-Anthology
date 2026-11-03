/* Port of src/content/q1/missionpacks/world/hipnotic-misc.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_misc.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||` on a number */
static bool flag(double spawnflags, double bit) { return qa_js_bit_and(spawnflags, bit) != 0; }
static double random_of(qa_q1_services *game) { return game->host.random(game->host.ctx); }
/* TS Q1SoundChannel: named channels, numerics as "5"/"6"/"7". */
static void sound(qa_q1_services *game, qa_q1_actor *entity, const char *path, qa_q1_sound_channel channel) {
  qa_q1_event event = {0};
  event.kind = "sound"; event.has_actor = true; event.actor = entity->actor.id; event.path = path; event.sound_channel = channel;
  event.volume = qa_q1_actor_number_or(entity, "volume", 1); event.attenuation = entity->speed;
  qa_q1_emit(game, event);
}
static void play_sound(qa_q1_services *game, qa_q1_actor *entity) {
  const char *path = qa_q1_actor_text(entity, "noise");
  if (flag(entity->spawnflags, 1)) {
    const bool active = qa_q1_actor_number(entity, "sound_state") == 0;
    qa_q1mp_world_number(entity, "sound_state", active ? 1 : 0);
    if (!active) path = "misc/null.wav";
  }
  const double channel = qa_q1_actor_number(entity, "impulse");
  sound(game, entity, path, channel == 0 ? "auto" : channel == 1 ? "weapon" : channel == 2 ? "voice" : channel == 3 ? "item" :
                            channel == 4 ? "body" : channel == 5 ? "5" : channel == 6 ? "6" : "7");
}
static void sound_spawn(qa_q1_services *game, qa_q1_actor *entity, bool periodic) {
  if (qa_q1_actor_number(entity, "volume") == 0) qa_q1mp_world_number(entity, "volume", 1);
  entity->speed = entity->speed == 0 ? 1 : entity->speed == -1 ? 0 : entity->speed;
  if (flag(entity->spawnflags, 1) && qa_q1_actor_number(entity, "impulse") == 0) qa_q1mp_world_number(entity, "impulse", 7);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:play_sound");
  if (periodic) {
    if (falsy(entity->wait)) entity->wait = 20;
    if (falsy(entity->delay)) entity->delay = 2;
    qa_q1mp_world_later(game, entity, qa_js_max(entity->delay, entity->wait * random_of(game)), "hip:play_sound");
  }
}
static void become_explosion(qa_q1_services *game, qa_q1_actor *entity) {
  entity->solid = "none"; entity->movement = "none"; entity->touch = (qa_q1_bound_touch){0};
  qa_q1mp_set_string(&entity->model, "progs/s_explod.spr"); entity->frame = 0;
  qa_q1_body_patch patch = {0}; patch.has_velocity = true; patch.velocity = QA_Q1_ZERO; qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  qa_q1mp_world_later(game, entity, 0.1, "base:explosion_frame");
}
static void explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, entity->has_activator ? &entity->activator : NULL);
  sound(game, entity, entity->damage < 120 ? "misc/shortexp.wav" : "misc/longexpl.wav", "auto");
  qa_q1_radius_damage(game, entity->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage, &entity->actor.id, NULL, "");
  if (flag(entity->spawnflags, 1)) qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  become_explosion(game, entity);
}
static void multi_explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_later(game, entity, entity->wait, "hip:multi_explode");
  if (qa_q1_actor_number(entity, "explosion_state") == 0) {
    qa_q1mp_world_number(entity, "explosion_state", 1);
    qa_q1mp_world_number(entity, "duration", game->time + qa_q1_actor_number(entity, "duration"));
    qa_q1_use_targets(game, entity, entity->has_activator ? &entity->activator : NULL);
  }
  if (game->time > qa_q1_actor_number(entity, "duration")) { qa_q1_remove(game, entity); return; }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 min = qa_q1_vadd(body.origin, body.bounds.min), max = qa_q1_vadd(body.origin, body.bounds.max);
  qa_q1_actor *explosion = qa_q1_create(game, "hip_explosion", NULL, NULL);
  explosion->has_owner = entity->has_owner; explosion->owner = entity->owner; explosion->damage = entity->damage;
  qa_vec3 origin;
  origin.x = min.x + random_of(game) * (max.x - min.x);
  origin.y = min.y + random_of(game) * (max.y - min.y);
  origin.z = min.z + random_of(game) * (max.z - min.z);
  qa_q1_set_origin(game, explosion, origin);
  qa_q1_event event = {0};
  event.kind = "sound"; event.has_actor = true; event.actor = explosion->actor.id; event.path = "misc/shortexp.wav"; event.sound_channel = "voice";
  event.volume = qa_q1_actor_number(entity, "volume"); event.attenuation = entity->speed;
  qa_q1_emit(game, event);
  qa_q1_radius_damage(game, explosion->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage, &entity->actor.id, NULL, "");
  if (flag(entity->spawnflags, 1)) qa_q1_effect(game, "explosion", qa_q1_get_body(game, explosion).origin, NULL, 1);
  become_explosion(game, explosion);
}
qa_q1_actor *qa_q1mp_multi_explosion(qa_q1_services *game, qa_q1_actor *source, qa_vec3 origin, double radius, double damage,
                                     double duration, double pause, double volume) {
  (void)source; /* TS `_source` is unused there too */
  qa_q1_actor *entity = qa_q1_create(game, "hip_multi_explosion", NULL, NULL);
  entity->damage = damage; entity->wait = pause;
  entity->has_owner = game->world != NULL;
  if (game->world != NULL) entity->owner = game->world->actor.id;
  qa_q1mp_world_number(entity, "duration", duration); qa_q1mp_world_number(entity, "volume", volume);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-radius, -radius, -radius}, {radius, radius, radius}};
  qa_q1_set_body(game, entity, &patch);
  multi_explode(NULL, game, entity);
  return entity;
}
void qa_q1mp_earthquake_after_physics(qa_q1_services *game, qa_actor_id actor) {
  qa_q1_actor *world = game->world;
  if (world == NULL) return;
  qa_owned_actor owner;
  const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner);
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!has_owner || !body.has_value) return;
  if (qa_q1_actor_number(world, "hip:earthquake") > game->time) {
    if (qa_q1_actor_number(world, "hip:quakeactive") == 0) {
      qa_q1_sound(game, owner.id, "misc/quake.wav", "voice", 0, 1);
      qa_q1mp_world_number(world, "hip:quakeactive", 1);
    }
    if (body.value.ground != NULL) {
      body.value.velocity = qa_q1_vadd(body.value.velocity, QA_Q1MP_V3(0, 0, random_of(game) * 150));
      qa_shared_body_table_write(game->host.bodies, owner, body.value);
    }
  } else if (qa_q1_actor_number(world, "hip:quakeactive") == 1) {
    qa_q1_sound(game, owner.id, "misc/quakeend.wav", "voice", 0, 1);
    qa_q1mp_world_number(world, "hip:quakeactive", 0);
  }
}
static void play_sound_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; play_sound(game, entity);
}
static void play_sound_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_later(game, entity, qa_js_max(entity->delay, entity->wait * random_of(game)), "hip:play_sound");
  play_sound(game, entity);
}
/* The TS spawn closures capture the classname (sound) or table row (ambient, rubble). */
typedef struct misc_class { const char *classname; const char *path; double index; } misc_class;
static misc_class SOUND_CLASSES[4] = {{"play_sound", NULL, 0}, {"play_sound_triggered", NULL, 0}, {"random_thunder", NULL, 0}, {"random_thunder_triggered", NULL, 0}};
static misc_class AMBIENT_CLASSES[6] = {
  {"ambient_humming", "ambient/humming.wav", 0}, {"ambient_rushing", "ambient/rushing.wav", 0},
  {"ambient_running_water", "ambient/runwater.wav", 0}, {"ambient_fan_blowing", "ambient/fanblow.wav", 0},
  {"ambient_waterfall", "ambient/waterfal.wav", 0}, {"ambient_riftpower", "ambient/riftpowr.wav", 0},
};
static misc_class EXPLODER_CLASSES[2] = {{"func_exploder", NULL, 0}, {"func_multi_exploder", NULL, 0}};
static misc_class RUBBLE_CLASSES[4] = {{"func_rubble", NULL, 0}, {"func_rubble1", NULL, 1}, {"func_rubble2", NULL, 2}, {"func_rubble3", NULL, 3}};
static bool ends_with(const char *text, const char *suffix) {
  const size_t length = strlen(text), suffix_length = strlen(suffix);
  return length >= suffix_length && strcmp(text + length - suffix_length, suffix) == 0;
}
static void play_sound_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const char *classname = ((const misc_class *)ctx)->classname;
  const bool thunder = qa_q1mp_starts(classname, "random_thunder");
  if (thunder) qa_q1mp_field_text(entity, "noise", "ambience/thunder1.wav");
  sound_spawn(game, entity, !ends_with(classname, "_triggered"));
  if (thunder) qa_q1mp_world_number(entity, "impulse", 6);
}
static void ambient_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const double volume = qa_q1_actor_number(entity, "volume");
  qa_q1_event event = {0};
  event.kind = "ambient"; event.has_origin = true; event.origin = qa_q1_get_body(game, entity).origin; event.path = ((const misc_class *)ctx)->path;
  event.volume = falsy(volume) ? 0.5 : volume; event.attenuation = 3;
  qa_q1_emit(game, event);
}
static void info_command_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (entity->message[0] == '\0') return;
  qa_q1_event event = {0};
  event.kind = "server-command"; event.text = entity->message;
  qa_q1_emit(game, event);
}
static void teleport_effect(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  qa_q1_effect(game, "teleport", qa_q1_get_body(game, entity).origin, NULL, 1);
  qa_q1_sound(game, entity->actor.id, "misc/r_tele1.wav", "voice", 1, 1);
}
static void effect_teleport_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:teleport_effect");
}
static void set_activator(qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
}
static void explode_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  set_activator(entity, activator);
  if (entity->delay == 0) { explode(ctx, game, entity); return; }
  const double delay = entity->delay; entity->delay = 0;
  qa_q1mp_world_later(game, entity, delay, "hip:explode");
}
static void multi_explode_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  set_activator(entity, activator);
  if (entity->delay == 0) { multi_explode(ctx, game, entity); return; }
  const double delay = entity->delay; entity->delay = 0;
  qa_q1mp_world_later(game, entity, delay, "hip:multi_explode");
}
static void exploder_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const bool multi = qa_q1mp_streq(((const misc_class *)ctx)->classname, "func_multi_exploder");
  entity->damage = entity->damage == 0 ? 120 : qa_js_max(0, entity->damage);
  if (falsy(entity->speed)) entity->speed = 1;
  if (qa_q1_actor_number(entity, "volume") == 0) qa_q1mp_world_number(entity, "volume", multi ? 0.5 : 1);
  if (multi) {
    qa_q1mp_set_string(&entity->model, ""); entity->movement = "none";
    if (falsy(entity->wait)) entity->wait = 0.25;
    if (qa_q1_actor_number(entity, "duration") == 0) qa_q1mp_world_number(entity, "duration", 1);
    qa_q1mp_world_number(entity, "explosion_state", 0);
  }
  entity->use = qa_q1_callback_registry_use(game->named, entity, multi ? "hip:multi_explode" : "hip:explode");
}
static void rubble_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, other);
  double left = qa_q1_actor_number(entity, "ltime");
  double right = qa_q1_actor_number(entity, "pausetime");
  if (left < right || !(combat.has_value && combat.value.can_take_damage)) return;
  (void)qa_q1_damage(game, other, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 10, NULL, "direct", "", NULL);
  qa_q1_sound(game, entity->actor.id, "zombie/z_hit.wav", "weapon", 1, 1);
  qa_q1mp_world_number(entity, "pausetime", qa_q1_actor_number(entity, "ltime") + 0.1);
}
static void rubble_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  for (double index = 0; index < qa_js_max(1, entity->count); index++) {
    const double cnt = qa_q1_actor_number(entity, "cnt");
    const double which = !falsy(cnt) ? cnt : floor(1 + 3 * random_of(game));
    qa_q1_actor *piece = qa_q1_create(game, "hip_rubble", NULL, NULL);
    qa_q1mp_set_string(&piece->model, which == 1 ? "progs/rubble1.mdl" : which == 2 ? "progs/rubble3.mdl" : "progs/rubble2.mdl");
    piece->movement = "bounce"; piece->solid = "bbox";
    qa_q1_body_patch patch = {0};
    patch.has_origin = true; patch.origin = qa_q1_get_body(game, entity).origin;
    patch.has_bounds = true; patch.bounds = QA_Q1_POINT;
    patch.has_velocity = true;
    patch.velocity.x = 70 * (random_of(game) * 2 - 1);
    patch.velocity.y = 70 * (random_of(game) * 2 - 1);
    patch.velocity.z = 140 + 70 * random_of(game);
    qa_q1_set_body(game, piece, &patch);
    piece->angular_velocity.x = random_of(game) * 600;
    piece->angular_velocity.y = random_of(game) * 600;
    piece->angular_velocity.z = random_of(game) * 600;
    piece->touch = qa_q1_callback_registry_touch(game->named, piece, "hip:rubble_touch");
    qa_q1mp_world_number(piece, "ltime", game->time); qa_q1mp_world_number(entity, "pausetime", game->time);
    qa_q1mp_world_later(game, piece, 13 + random_of(game) * 10, "SUB_Remove");
    qa_q1_link(game, piece);
  }
}
static void rubble_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1mp_world_number(entity, "cnt", ((const misc_class *)ctx)->index);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:rubble_use");
}
static void earthquake_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  qa_q1_actor *world = game->world;
  if (world != NULL) qa_q1mp_world_number(world, "hip:earthquake", qa_js_max(qa_q1_actor_number(world, "hip:earthquake"), game->time + entity->damage));
}
static void earthquake_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (falsy(entity->damage)) entity->damage = 0.8;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:earthquake");
  if (game->world != NULL) qa_q1mp_world_number(game->world, "hip:quakeactive", 0);
}
void qa_q1mp_register_hipnotic_misc(qa_q1_services *game) {
  qa_q1_callback_registry *named = game->named;
  qa_q1_callback_registry_register(named, "hip:play_sound", (qa_q1_callback_handlers){.use = play_sound_use, .action = play_sound_action});
  for (size_t i = 0; i < 4; i++) qa_q1_register_spawn(game, SOUND_CLASSES[i].classname, play_sound_spawn, &SOUND_CLASSES[i]);
  for (size_t i = 0; i < 6; i++) qa_q1_register_spawn(game, AMBIENT_CLASSES[i].classname, ambient_spawn, &AMBIENT_CLASSES[i]);
  qa_q1_register_spawn(game, "info_command", info_command_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:teleport_effect", (qa_q1_callback_handlers){.use = teleport_effect});
  qa_q1_register_spawn(game, "effect_teleport", effect_teleport_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:explode", (qa_q1_callback_handlers){.action = explode, .use = explode_use});
  qa_q1_callback_registry_register(named, "hip:multi_explode", (qa_q1_callback_handlers){.action = multi_explode, .use = multi_explode_use});
  for (size_t i = 0; i < 2; i++) qa_q1_register_spawn(game, EXPLODER_CLASSES[i].classname, exploder_spawn, &EXPLODER_CLASSES[i]);
  qa_q1_callback_registry_register(named, "hip:rubble_touch", (qa_q1_callback_handlers){.touch = rubble_touch});
  qa_q1_callback_registry_register(named, "hip:rubble_use", (qa_q1_callback_handlers){.use = rubble_use});
  for (size_t i = 0; i < 4; i++) qa_q1_register_spawn(game, RUBBLE_CLASSES[i].classname, rubble_spawn, &RUBBLE_CLASSES[i]);
  qa_q1_callback_registry_register(named, "hip:earthquake", (qa_q1_callback_handlers){.use = earthquake_use});
  qa_q1_register_spawn(game, "func_earthquake", earthquake_spawn, NULL);
}
