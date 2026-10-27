/* Port of src/content/q1/addons/lights.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/lights.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

char qa_q1ad_light_style(double fraction, bool full_range) {
  int maximum = full_range ? 25 : 12;
  int step = fraction <= 0 ? 0 : fraction >= 1 ? maximum : (int)floor(fraction * (maximum + 1));
  return (char)(97 + step);
}
static double fr(double value) { return (double)(float)value; }
static void prefix_of(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:light:", qa_q1ad_program_name(context->program));
}
static void lightstyle(qa_q1ad_context *context, double style, const char *pattern) {
  qa_q1ad_host_event event = {.kind = "lightstyle", .style = style, .pattern = pattern};
  qa_q1ad_host_emit(context, &event);
}
static void ambient(qa_q1ad_context *context, qa_vec3 origin, const char *path, double volume) {
  qa_q1ad_host_event event = {.kind = "ambient", .origin = origin, .path = path,
                              .volume = volume, .attenuation = 3};
  qa_q1ad_host_emit(context, &event);
}
static void spawn_dynamiclight(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.coop && ((int32_t)entity->spawnflags & 1) != 0) qa_q1ad_remove(context, entity);
}
static void ramp_tick(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_actor *owner = entity->has_owner ? qa_q1ad_entity(context, &entity->owner) : NULL;
  if (owner == NULL) qa_range_error("target_lightramp lost its light");
  double state = qa_q1ad_actor_number(entity, "ramp.state");
  double delta = fr(context->frame_time * entity->delay);
  double fraction = fr(qa_q1ad_actor_number(entity, "cnt") + (state == 3 ? -delta : state == 1 ? delta : 0));
  char pattern[2] = {qa_q1ad_light_style(fraction, ((int32_t)entity->spawnflags & 1) != 0), '\0'};
  lightstyle(context, qa_q1ad_actor_number(owner, "style"), pattern);
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
static void ramp_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  double state = qa_q1ad_actor_number(entity, "ramp.state");
  qa_q1ad_set_number(context, entity, "ramp.state", state == 3 || state == 0 ? 1 : 3);
  if (state == 0 || state == 2) {
    char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
    prefix_of(context, prefix);
    qa_q1ad_named_name(full, prefix, "ramp_tick");
    qa_q1ad_add_frame_tick(context, entity, full);
  }
}
static void ramp_init(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const char *targetname = entity->targetname != NULL ? entity->targetname : "";
  if (entity->target == NULL || entity->target[0] == '\0' || targetname[0] == '\0')
    qa_range_error("target_lightramp requires target and targetname");
  qa_q1ad_actor *found[4];
  size_t count = qa_q1ad_find(context, entity->target, found, 4);
  if (count == 0) qa_range_error("target_lightramp has an unmatched target");
  qa_q1ad_actor *owner = found[0];
  entity->owner = owner->actor.id;
  entity->has_owner = true;
  bool off = ((int32_t)owner->spawnflags & 1) != 0;
  qa_q1ad_set_number(context, entity, "ramp.state", off ? 0 : 2);
  qa_q1ad_set_number(context, entity, "cnt", off ? 0 : 1);
  qa_q1ad_bind(entity->think, "SUB_Null");
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "ramp_use");
}
static void spawn_lightramp(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->delay = fr(1 / (entity->delay != 0 ? entity->delay : 1));
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_schedule_named(context, entity, 0.1, prefix, "ramp_init");
}
static void spawn_light(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->targetname == NULL || entity->targetname[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  double style = qa_q1ad_actor_number(entity, "style");
  if (style < 32) return;
  if (((int32_t)entity->spawnflags & 2) != 0) {
    lightstyle(context, style, entity->targetname);
    qa_q1ad_remove(context, entity);
    return;
  }
  lightstyle(context, style, ((int32_t)entity->spawnflags & 1) != 0 ? "a" : "m");
  qa_q1ad_bind(entity->use, "light_use");
}
static void spawn_fluorospark(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_number(entity, "style") == 0) qa_q1ad_set_number(context, entity, "style", 10);
  ambient(context, qa_q1ad_body_of(context, entity).origin, "ambience/buzz1.wav", 0.5);
}
static void spawn_flame(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const char *classname = entity->classname != NULL ? entity->classname : "";
  entity->model = strcmp(classname, "light_torch_small_walltorch") == 0 ? "progs/flame.mdl" : "progs/flame2.mdl";
  if (context->game.precache_model != NULL) (void)context->game.precache_model(context->game.ctx, entity->model);
  if (strcmp(classname, "light_flame_large_yellow") == 0) entity->frame = 1;
  if (((int32_t)entity->spawnflags & 4) != 0) {
    qa_q1ad_body_patch patch = {.has_angles = true, .angles = {180, 0, 0}};
    qa_q1ad_set_body(context, entity, &patch);
  }
  if (context->game.precache_sound != NULL) (void)context->game.precache_sound(context->game.ctx, "ambience/fire1.wav");
  ambient(context, qa_q1ad_body_of(context, entity).origin, "ambience/fire1.wav", 0.5);
  if (context->game.make_static != NULL) context->game.make_static(context->game.ctx, entity);
}
static void spawn_flame_gas(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->model = "progs/flame3.mdl";
  qa_q1ad_alpha(context, entity, 0.6);
  if (context->game.create == NULL) return;
  qa_q1ad_actor *second = context->game.create(context->game.ctx, "gas_flame");
  if (second == NULL) return;
  second->model = entity->model;
  second->frame = 1;
  qa_q1ad_set_origin(context, second, qa_q1ad_body_of(context, entity).origin);
  qa_q1ad_alpha(context, second, 0.4);
  qa_q1ad_link(context, second);
}
static void spawn_candle(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->model = context->game.precache_model != NULL
      ? context->game.precache_model(context->game.ctx, "progs/candle.mdl") : "progs/candle.mdl";
  if (context->game.make_static != NULL) context->game.make_static(context->game.ctx, entity);
}

void qa_q1ad_register_lights(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("ramp_tick", ((qa_q1ad_handlers){.action = ramp_tick}));
  REG("ramp_use", ((qa_q1ad_handlers){.use = ramp_use}));
  REG("ramp_init", ((qa_q1ad_handlers){.action = ramp_init}));
#undef REG
  qa_q1ad_register_spawn(context, "dynamiclight", false, spawn_dynamiclight);
  qa_q1ad_register_spawn(context, "target_lightramp", false, spawn_lightramp);
  qa_q1ad_register_spawn(context, "light", false, spawn_light);
  qa_q1ad_register_spawn(context, "light_fluorospark", false, spawn_fluorospark);
  qa_q1ad_register_spawn(context, "light_torch_small_walltorch", true, spawn_flame);
  qa_q1ad_register_spawn(context, "light_flame_large_yellow", true, spawn_flame);
  qa_q1ad_register_spawn(context, "light_flame_small_yellow", true, spawn_flame);
  qa_q1ad_register_spawn(context, "light_flame_small_white", true, spawn_flame);
  qa_q1ad_register_spawn(context, "light_flame_gas", false, spawn_flame_gas);
  qa_q1ad_register_spawn(context, "light_candle", false, spawn_candle);
}
