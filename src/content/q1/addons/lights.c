/* Port of src/content/q1/addons/lights.ts (quakec_mg1/lights.qc and
 * quakec_mg3/lights.qc). Copyright (C) 1996-2026 id Software LLC.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/lights.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/base/map_entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* `${program}:light:` names: static literals, since slots borrow the name. */
#define NAMED(context, name) \
  ((const char *const[]){"dopa:light:" name, "mg1:light:" name, "mg3:light:" name, \
                         "ctf:light:" name}[(context)->program])

/* TS addonLightStyle returns a one-character string; C returns its char. */
char qa_q1ad_light_style(double fraction, bool full_range) {
  double maximum = full_range ? 25 : 12;
  double code = 97 + (fraction <= 0 ? 0 : fraction >= 1 ? maximum : floor(fraction * (maximum + 1)));
  /* String.fromCharCode applies ToUint16: a NaN fraction gives U+0000, not a NaN cast. */
  return isnan(code) ? '\0' : (char)code;
}

static double fr(double value) { return (double)(float)value; }
static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }

static void lightstyle(qa_q1_services *game, double style, const char *pattern) {
  qa_q1_event event = {.kind = "lightstyle", .style = style, .pattern = pattern};
  qa_q1_emit(game, event);
}
static void ambient(qa_q1_services *game, qa_vec3 origin, const char *path) {
  qa_q1_event event = {.kind = "ambient", .has_origin = true, .origin = origin, .path = path, .volume = 0.5, .attenuation = 3};
  qa_q1_emit(game, event);
}

static void spawn_dynamiclight(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (game->options.coop && (spawnflags(entity) & 1) != 0) qa_q1_remove(game, entity);
}

static void ramp_tick(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  /* Only target_lightramp_init objerrors on a missing light; a light killed later is read as a freed edict, so the ramp just stops. */
  qa_q1_actor *owner = entity->has_owner ? qa_q1_get_entity(game, &entity->owner) : NULL;
  if (owner == NULL) { qa_q1ad_remove_frame_tick(context, entity); return; }
  double state = qa_q1_actor_number(entity, "ramp.state"), delta = fr(context->frame_time * entity->delay);
  double fraction = fr(qa_q1_actor_number(entity, "cnt") + (state == 3 ? -delta : state == 1 ? delta : 0));
  char pattern[2] = {qa_q1ad_light_style(fraction, (spawnflags(entity) & 1) != 0), '\0'};
  lightstyle(game, qa_q1_actor_number(owner, "style"), pattern);
  qa_q1ad_set_number(context, entity, "cnt", fraction);
  if (fraction >= 1) {
    qa_q1ad_set_number(context, entity, "cnt", 1);
    qa_q1ad_set_number(context, entity, "ramp.state", 2);
    qa_q1ad_remove_frame_tick(context, entity);
  }
  if (fraction <= 0) {
    qa_q1ad_set_number(context, entity, "cnt", 0);
    qa_q1ad_set_number(context, entity, "ramp.state", 0);
    qa_q1ad_remove_frame_tick(context, entity);
  }
}
static void ramp_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  double state = qa_q1_actor_number(entity, "ramp.state");
  qa_q1ad_set_number(context, entity, "ramp.state", state == 3 || state == 0 ? 1 : 3);
  if (state == 0 || state == 2) qa_q1ad_add_frame_tick(context, entity, NAMED(context, "ramp_tick"));
}
static void ramp_init(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (entity->target[0] == '\0' || entity->targetname[0] == '\0') qa_range_error("target_lightramp requires target and targetname");
  qa_q1_actor *found[1];
  if (qa_q1_find(game, entity->target, found, 1) == 0) qa_range_error("target_lightramp has an unmatched target");
  qa_q1_actor *owner = found[0];
  entity->has_owner = true;
  entity->owner = owner->actor.id;
  bool off = (spawnflags(owner) & 1) != 0;
  qa_q1ad_set_number(context, entity, "ramp.state", off ? 0 : 2);
  qa_q1ad_set_number(context, entity, "cnt", off ? 0 : 1);
  entity->think = qa_q1_callback_registry_action(game->named, entity, "SUB_Null");
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "ramp_use"));
}
static void spawn_lightramp(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->delay = fr(1 / (entity->delay == 0 || isnan(entity->delay) ? 1 : entity->delay));
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, NAMED(context, "ramp_init")));
}
static void spawn_light(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (entity->targetname[0] == '\0') { qa_q1_remove(game, entity); return; }
  double style = qa_q1_actor_number(entity, "style");
  if (style < 32) return;
  if ((spawnflags(entity) & 2) != 0) {
    lightstyle(game, style, entity->targetname);
    qa_q1_remove(game, entity);
    return;
  }
  lightstyle(game, style, (spawnflags(entity) & 1) != 0 ? "a" : "m");
  entity->use = qa_q1_callback_registry_use(game->named, entity, "light_use");
}
static void spawn_fluorospark(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_number(entity, "style") == 0) qa_q1ad_set_number(context, entity, "style", 10);
  ambient(game, qa_q1_get_body(game, entity).origin, "ambience/buzz1.wav");
}
static void spawn_flame(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1ad_set_text(&entity->model, strcmp(entity->classname, "light_torch_small_walltorch") == 0 ? "progs/flame.mdl" : "progs/flame2.mdl");
  (void)qa_q1_services_precache_model(game, entity->model);
  if (strcmp(entity->classname, "light_flame_large_yellow") == 0) entity->frame = 1;
  if ((spawnflags(entity) & 4) != 0) {
    qa_q1_body_patch patch = {.has_angles = true, .angles = {180, 0, 0}};
    qa_q1_set_body(game, entity, &patch);
  }
  (void)qa_q1_services_precache_sound(game, "ambience/fire1.wav");
  ambient(game, qa_q1_get_body(game, entity).origin, "ambience/fire1.wav");
  qa_q1_make_static(game, entity);
}
static void spawn_flame_gas(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_set_text(&entity->model, "progs/flame3.mdl");
  qa_q1ad_alpha(context, entity, 0.6);
  qa_q1_actor *second = qa_q1_create(game, "gas_flame", NULL, NULL);
  qa_q1ad_set_text(&second->model, entity->model);
  second->frame = 1;
  qa_q1_set_origin(game, second, qa_q1_get_body(game, entity).origin);
  qa_q1ad_alpha(context, second, 0.4);
  qa_q1_link(game, second);
}
static void spawn_candle(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1ad_set_text(&entity->model, qa_q1_services_precache_model(game, "progs/candle.mdl"));
  qa_q1_make_static(game, entity);
}

void qa_q1ad_register_lights(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_register_spawn(game, "dynamiclight", spawn_dynamiclight, context);
  qa_q1_callback_registry_register(game->named, NAMED(context, "ramp_tick"), (qa_q1_callback_handlers){.ctx = context, .action = ramp_tick});
  qa_q1_callback_registry_register(game->named, NAMED(context, "ramp_use"), (qa_q1_callback_handlers){.ctx = context, .use = ramp_use});
  qa_q1_callback_registry_register(game->named, NAMED(context, "ramp_init"), (qa_q1_callback_handlers){.ctx = context, .action = ramp_init});
  qa_q1_register_spawn(game, "target_lightramp", spawn_lightramp, context);
  qa_q1_register_spawn(game, "light", spawn_light, context);
  qa_q1_register_spawn(game, "light_fluorospark", spawn_fluorospark, context);
  static const char *const FLAMES[] = {"light_torch_small_walltorch", "light_flame_large_yellow", "light_flame_small_yellow",
                                       "light_flame_small_white"};
  for (size_t i = 0; i < 4; i++) qa_q1_replace_spawn(game, FLAMES[i], spawn_flame, context);
  qa_q1_register_spawn(game, "light_flame_gas", spawn_flame_gas, context);
  qa_q1_register_spawn(game, "light_candle", spawn_candle, context);
}
