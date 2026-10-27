/* Port of src/content/q1/addons/brushes.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/brushes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

static void prefix_of(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:brush:", qa_q1ad_program_name(context->program));
}
static void schedule(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, double delay) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_schedule_named(context, entity, delay, prefix, name);
}
static void schedule_at_named(qa_q1ad_context *context, qa_q1ad_actor *entity, double time, const char *name) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_named_name(full, prefix, name);
  qa_q1ad_schedule_at(context, entity, time, full);
}
static void call_named(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_named_name(full, prefix, name);
  qa_q1ad_do_action(context, entity, full);
}
static double fr(double value) { return (double)(float)value; }
static void push_brush(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->solid = "bsp";
  entity->movement = "push";
  qa_q1ad_link(context, entity);
}
static qa_vec3 bob_position(qa_q1ad_actor *entity, double angle) {
  entity->count = fr(fmod(fr(entity->count + angle), 360));
  return qa_add3(qa_scale3(qa_q1ad_actor_vector(entity, "dest"), fr(sin(entity->count))),
                 qa_scale3(qa_q1ad_actor_vector(entity, "dest2"), fr(cos(entity->count))));
}
static void bob_tick(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_set_origin(context, entity, bob_position(entity, fr(entity->angular_velocity.x * context->frame_time)));
}
static void bob_think(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_vec3 position = bob_position(entity, fr(entity->angular_velocity.x * 0.05));
  qa_q1ad_body_patch patch = {.has_velocity = true,
      .velocity = qa_scale3(qa_sub3(position, qa_q1ad_body_of(context, entity).origin), 20)};
  qa_q1ad_set_body(context, entity, &patch);
  schedule_at_named(context, entity, fr(qa_q1ad_actor_number(entity, "ltime") + 0.05), "bob_think");
}
static void bob_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                    const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  bool solid = entity->solid == NULL || strcmp(entity->solid, "none") != 0;
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  if (entity->activated) {
    if (solid) {
      qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = {0, 0, 0}};
      qa_q1ad_set_body(context, entity, &patch);
      qa_q1ad_cancel(context, entity);
    } else qa_q1ad_remove_frame_tick(context, entity);
  } else if (solid) call_named(context, entity, "bob_think");
  else { qa_q1ad_named_name(full, prefix, "bob_tick"); qa_q1ad_add_frame_tick(context, entity, full); }
  entity->activated = !entity->activated;
}
static void bob_blocked(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_actor_id other) {
  if (entity->attack_finished > context->game.time) return;
  qa_actor_id self = entity->actor.id;
  qa_q1ad_damage(context, other, &self, &self, entity->damage, NULL, NULL, NULL);
  entity->attack_finished = fr(context->game.time + 0.5);
}
static void spawn_bob(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_length3(qa_q1ad_actor_vector(entity, "dest")) == 0)
    qa_q1ad_set_vector(context, entity, "dest", (qa_vec3){0, 0, 64});
  if (entity->wait == 0) entity->wait = 10;
  if (entity->damage == 0) entity->damage = 1;
  entity->angular_velocity = (qa_vec3){fr(360 / entity->wait), 0, 0};
  entity->count = fr(360 * entity->delay);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "bob_use");
  qa_q1ad_bind_prefix(entity->blocked, prefix, "bob_blocked");
  entity->solid = ((int32_t)entity->spawnflags & 1) != 0 ? "none" : "bsp";
  entity->movement = strcmp(entity->solid, "none") == 0 ? "none" : "push";
  if (((int32_t)entity->spawnflags & 2) != 0) qa_q1ad_do_use(context, entity, entity->use, NULL, NULL);
}
static void toss_think(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_length3(qa_q1ad_body_of(context, entity).velocity) < 0.01) {
    int32_t flags = (int32_t)entity->spawnflags;
    if ((flags & 2) != 0 && ((flags & 8) == 0 ||
        qa_length3(qa_sub3(qa_q1ad_actor_vector(entity, "oldorigin"),
                           qa_q1ad_body_of(context, entity).origin)) > 64)) {
      qa_q1ad_remove(context, entity);
      return;
    }
    qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = {0, 0, 0}};
    qa_q1ad_set_body(context, entity, &patch);
    push_brush(context, entity);
    return;
  }
  schedule(context, entity, "toss_think", 0.5);
}
static void toss_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  entity->solid = "bbox";
  entity->movement = ((int32_t)entity->spawnflags & 4) != 0 ? "toss" : "bounce";
  qa_bounds bounds = {qa_add3(body.bounds.min, (qa_vec3){4, 4, 0}), body.bounds.max};
  qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = entity->movedir,
                              .has_bounds = true, .bounds = bounds};
  qa_q1ad_set_body(context, entity, &patch);
  if (qa_q1ad_actor_text(entity, "noise")[0] != '\0')
    qa_q1ad_sound_entity(context, entity, qa_q1ad_actor_text(entity, "noise"), NULL);
  qa_q1ad_link(context, entity);
  schedule(context, entity, "toss_think", 0.25);
}
static void toss_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  entity->use[0] = '\0';
  if (entity->delay != 0)
    schedule_at_named(context, entity, fr(qa_q1ad_actor_number(entity, "ltime") + entity->delay), "toss");
  else call_named(context, entity, "toss");
}
static void toss_cascade(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 position = qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5);
  double nearest = 16384;
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *other = entities[i];
    if (strcmp(qa_q1ad_actor_text(other, "netname"), "_toss_origin") != 0) continue;
    const char *a = other->targetname != NULL ? other->targetname : "";
    const char *b = entity->targetname != NULL ? entity->targetname : "";
    if (strcmp(a, b) != 0) continue;
    qa_bounds other_bounds = qa_q1ad_body_of(context, other).bounds;
    double value = fr(qa_length3(qa_sub3(position,
        qa_scale3(qa_add3(other_bounds.min, other_bounds.max), 0.5))) / other->speed);
    if (value < nearest) nearest = value;
  }
  if (nearest < 16384) entity->delay = nearest;
}
static double random_signed(qa_q1ad_context *context) { return qa_q1ad_random(context) * 2 - 1; }
static void spawn_toss(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  push_brush(context, entity);
  qa_q1ad_set_vector(context, entity, "oldorigin", qa_q1ad_body_of(context, entity).origin);
  entity->movedir = qa_q1ad_actor_vector(entity, "movedir");
  if (qa_length3(entity->movedir) == 0) entity->movedir = (qa_vec3){0, 0, 200};
  qa_vec3 variance = qa_q1ad_actor_vector(entity, "dest");
  if (qa_length3(variance) != 0)
    entity->movedir = qa_add3(entity->movedir, (qa_vec3){
        random_signed(context) * variance.x, random_signed(context) * variance.y,
        random_signed(context) * variance.z});
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "toss_use");
  if (((int32_t)entity->spawnflags & 1) != 0) {
    qa_q1ad_actor_set(entity, "netname", "_toss_origin");
    if (entity->speed == 0) entity->speed = 200;
    return;
  }
  if (entity->delay == 0)
    schedule_at_named(context, entity, fr(qa_q1ad_actor_number(entity, "ltime") + 0.2), "toss_cascade");
}
static void shatter_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  entity->use[0] = '\0';
  entity->solid = "none";
  entity->movement = "toss";
  qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = entity->movedir};
  qa_q1ad_set_body(context, entity, &patch);
  qa_q1ad_link(context, entity);
}
static void spawn_shatter(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  push_brush(context, entity);
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  entity->pos1 = qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5));
  entity->pos2 = qa_q1ad_actor_vector(entity, "pos2");
  if (qa_length3(entity->pos2) == 0) entity->pos2 = qa_sub3(body.origin, (qa_vec3){0, 0, 100});
  qa_vec3 difference = qa_sub3(entity->pos1, entity->pos2);
  double distance = qa_length3(difference);
  entity->speed = fr(fr((entity->speed != 0 ? entity->speed : 200) * 10000) / fr(distance * distance));
  if (entity->wait == 0) entity->wait = 10;
  entity->movedir = qa_add3(qa_scale3(qa_normalize3_or_zero(difference), entity->speed), (qa_vec3){
      random_signed(context) * entity->wait, random_signed(context) * entity->wait,
      random_signed(context) * entity->wait});
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "shatter");
}
static void debris_fade(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  double alpha = fr(qa_q1ad_actor_number_or(entity, "alpha", 1) -
                    fr(fr(1 / entity->wait) * context->frame_time));
  qa_q1ad_alpha(context, entity, alpha);
  if (alpha < 0) {
    qa_q1ad_alpha(context, entity, 1);
    qa_q1ad_set_origin(context, entity, qa_q1ad_actor_vector(entity, "oldorigin"));
    entity->movement = "toss";
    qa_q1ad_body_patch patch = {.has_velocity = true,
        .velocity = {random_signed(context) * 8, random_signed(context) * 8, 0}};
    qa_q1ad_set_body(context, entity, &patch);
    schedule(context, entity, "debris_fade", entity->delay);
    return;
  }
  schedule(context, entity, "debris_fade", 0.01);
}
static void debris_wake(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->use[0] = '\0';
  entity->solid = "none";
  entity->movement = "toss";
  qa_q1ad_alpha(context, entity, 1);
  qa_vec3 velocity = qa_q1ad_body_of(context, entity).velocity;
  velocity.x = random_signed(context) * 8;
  velocity.y = random_signed(context) * 8;
  qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = velocity};
  qa_q1ad_set_body(context, entity, &patch);
  qa_q1ad_link(context, entity);
  schedule(context, entity, "debris_fade", entity->delay);
}
static void debris_wait(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  schedule_at_named(context, entity, fr(qa_q1ad_actor_number(entity, "ltime") + qa_q1ad_random(context)), "debris_wake");
}
static void spawn_debris(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  push_brush(context, entity);
  qa_q1ad_set_vector(context, entity, "oldorigin", qa_q1ad_body_of(context, entity).origin);
  if (entity->delay == 0) entity->delay = 1.5;
  if (entity->wait == 0) entity->wait = 0.1;
  entity->movedir = (qa_vec3){0, 0, 200};
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "debris_wait");
  qa_q1ad_alpha(context, entity, 1);
}
static void explode_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 position = qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5);
  entity->model = "";
  qa_q1ad_set_origin(context, entity, position);
  qa_actor_id self = entity->actor.id;
  if (context->game.radius_damage != NULL)
    context->game.radius_damage(context->game.ctx, self, &self, 160, NULL, NULL);
  qa_q1ad_sound_entity(context, entity, "weapons/r_exp3.wav", NULL);
  qa_q1ad_effect(context, "explosion", position, NULL, 0);
  qa_q1ad_use_targets(context, entity, entity->has_activator ? &entity->activator : NULL);
  qa_q1ad_remove(context, entity);
}
static void explode_die(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *attacker) {
  entity->damageable = false;
  if (attacker != NULL) { entity->activator = *attacker; entity->has_activator = true; }
  else entity->has_activator = false;
  schedule_at_named(context, entity, fr(qa_q1ad_actor_number(entity, "ltime") + 0.15), "explode");
}
static void spawn_explode(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  push_brush(context, entity);
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = {0, 0, 0}};
  qa_q1ad_set_body(context, entity, &patch);
  if (context->game.combat_set_health != NULL)
    context->game.combat_set_health(context->game.ctx, entity->actor, 20);
  entity->damageable = true;
  entity->aimed_damage = true;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->die, prefix, "explode_die");
}
static void hurt_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)context; (void)other; (void)activator;
  entity->activated = !entity->activated;
  entity->attack_finished = 0;
}
static void hurt_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  bool can_take = false;
  bool known = context->game.combat_can_take_damage != NULL &&
      context->game.combat_can_take_damage(context->game.ctx, *other, &can_take);
  if (!entity->activated || entity->attack_finished > context->game.time ||
      qa_q1ad_health(context, *other) <= 0 || !known || !can_take) return;
  bool monster = context->services.is_monster != NULL &&
      context->services.is_monster(context->services.ctx, *other);
  if (!qa_q1ad_is_player(context, other) && !monster) return;
  qa_q1ad_actor *world = qa_q1ad_world(context);
  qa_actor_id self = entity->actor.id;
  qa_actor_id world_id = world != NULL ? world->actor.id : self;
  qa_q1ad_damage(context, *other, &self, world != NULL ? &world_id : NULL, entity->damage, NULL, NULL, NULL);
  entity->attack_finished = fr(context->game.time + entity->wait);
}
static void spawn_hurt(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  push_brush(context, entity);
  if (entity->damage == 0) entity->damage = 10;
  if (entity->wait == 0) entity->wait = 0.2;
  entity->activated = ((int32_t)entity->spawnflags & 1) != 0;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "hurt_use");
  qa_q1ad_bind_prefix(entity->touch, prefix, "hurt_touch");
}
static void spawn_fade_brush(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_bind(entity->use, "func_wall_use");
  push_brush(context, entity);
}
static void model_loop(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->frame++;
  if (entity->frame == qa_q1ad_actor_number(entity, "cnt")) entity->frame = entity->count;
  schedule(context, entity, "model_loop", 0.1);
}
static void model_once(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->frame++;
  if (entity->frame < qa_q1ad_actor_number(entity, "cnt")) schedule(context, entity, "model_once", 0.1);
}
static void model_use_once(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  entity->frame = entity->count;
  schedule(context, entity, "model_once", 0.1);
}
static void model_use_loop(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  entity->spawnflags = (double)((int32_t)entity->spawnflags ^ 4);
  if (((int32_t)entity->spawnflags & 4) == 0) call_named(context, entity, "model_loop");
  else qa_q1ad_cancel(context, entity);
}
static void spawn_model(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->model == NULL || entity->model[0] == '\0') qa_range_error("misc_model requires a model");
  entity->frame = qa_q1ad_actor_number(entity, "frame");
  entity->solid = "none";
  if (entity->spawnflags == 0) return;
  if (qa_q1ad_actor_number(entity, "cnt") < entity->frame ||
      entity->targetname == NULL || entity->targetname[0] == '\0')
    qa_range_error("misc_model requires a valid frame range and targetname");
  entity->count = entity->frame;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  if (((int32_t)entity->spawnflags & 2) != 0) qa_q1ad_bind_prefix(entity->use, prefix, "model_use_once");
  else if (((int32_t)entity->spawnflags & 5) != 0) {
    qa_q1ad_bind_prefix(entity->use, prefix, "model_use_loop");
    entity->spawnflags = (double)((int32_t)entity->spawnflags ^ 4);
    qa_q1ad_do_use(context, entity, entity->use, NULL, NULL);
  }
}
static void spawn_remove(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_remove(context, entity);
}
static double wrap_angle(double value) {
  if (value > 360) return value - ceil((value - 360) / 360) * 360;
  if (value < 0) return value + ceil(-value / 360) * 360;
  return value;
}
static void rotate_apply(qa_q1ad_context *context, qa_q1ad_actor *entity, double scale) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 angles = qa_add3(body.angles, qa_scale3(entity->angular_velocity, fr(context->frame_time * scale)));
  /* Source ModAngles mistakenly tests x for the negative-z adjustment. */
  qa_vec3 wrapped = {wrap_angle(angles.x), wrap_angle(angles.y),
                     angles.z > 360 ? wrap_angle(angles.z) : angles.z};
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = wrapped};
  qa_q1ad_set_body(context, entity, &patch);
}
static void rotate_tick(qa_q1ad_context *context, qa_q1ad_actor *entity) { rotate_apply(context, entity, 1); }
static void rotate_tween(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  double next = fr(entity->speed + fr(qa_q1ad_actor_number(entity, "distance") * context->frame_time));
  entity->speed = next < 0 ? 0 : next > 1 ? 1 : next;
  if (entity->speed == 0) {
    qa_q1ad_set_number(context, entity, "rotate.state", 0);
    qa_q1ad_remove_frame_tick(context, entity);
    return;
  }
  if (entity->speed == 1) {
    qa_q1ad_set_number(context, entity, "rotate.state", 2);
    char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
    prefix_of(context, prefix);
    qa_q1ad_named_name(full, prefix, "rotate_tick");
    qa_q1ad_actor_set(entity, "addon.frameTick", full);
    rotate_apply(context, entity, 1);
    return;
  }
  rotate_apply(context, entity, entity->speed);
}
static void rotate_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  double state = qa_q1ad_actor_number(entity, "rotate.state");
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  if (entity->delay <= 0) {
    qa_q1ad_set_number(context, entity, "rotate.state", state == 0 ? 2 : 0);
    if (state == 0) { qa_q1ad_named_name(full, prefix, "rotate_tick"); qa_q1ad_add_frame_tick(context, entity, full); }
    else qa_q1ad_remove_frame_tick(context, entity);
    return;
  }
  qa_q1ad_named_name(full, prefix, "rotate_tween");
  qa_q1ad_actor_set(entity, "addon.frameTick", full);
  bool forward = state == 0 || state == 3;
  qa_q1ad_set_number(context, entity, "rotate.state", forward ? 1 : 3);
  double distance = qa_q1ad_actor_number(entity, "distance");
  qa_q1ad_set_number(context, entity, "distance", fabs(distance) * (forward ? 1 : -1));
  if (state == 0) qa_q1ad_add_frame_tick(context, entity, full);
}
static void spawn_rotate(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->angular_velocity = qa_q1ad_actor_vector(entity, "avelocity");
  if (qa_length3(entity->angular_velocity) == 0) entity->angular_velocity = (qa_vec3){0, 30, 0};
  if (entity->delay > 0) qa_q1ad_set_number(context, entity, "distance", 1 / entity->delay);
  entity->solid = ((int32_t)entity->spawnflags & 4) != 0 ? "bsp" : "none";
  entity->movement = strcmp(entity->solid, "bsp") == 0 ? "push" : "none";
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = {0, 0, 0}};
  qa_q1ad_set_body(context, entity, &patch);
  qa_vec3 pos = qa_q1ad_actor_vector(entity, "pos2");
  if (qa_length3(pos) != 0) qa_q1ad_set_origin(context, entity, pos);
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "rotate_use");
  bool off = ((int32_t)entity->spawnflags & 1) != 0;
  qa_q1ad_set_number(context, entity, "rotate.state", off ? 0 : 2);
  if (!off) {
    entity->speed = 1;
    qa_q1ad_named_name(full, prefix, "rotate_tick");
    qa_q1ad_add_frame_tick(context, entity, full);
  }
}
static void breakable_stop(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = {0, 0, 0}};
  qa_q1ad_set_body(context, entity, &patch);
}
static void breakable_pain(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.combat_set_health != NULL)
    context->game.combat_set_health(context->game.ctx, entity->actor, 10000);
  qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = {0, 0, -20}};
  qa_q1ad_set_body(context, entity, &patch);
  schedule_at_named(context, entity, fr(qa_q1ad_actor_number(entity, "ltime") + 1), "breakable_stop");
}
static void breakable_die(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *attacker) {
  (void)attacker;
  qa_q1ad_remove(context, entity);
}
static void spawn_breakable(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  push_brush(context, entity);
  if (context->game.combat_set_health != NULL)
    context->game.combat_set_health(context->game.ctx, entity->actor, 10000);
  entity->max_health = 10000;
  entity->damageable = true;
  entity->aimed_damage = false;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->pain, prefix, "breakable_pain");
  qa_q1ad_bind_prefix(entity->die, prefix, "breakable_die");
}

void qa_q1ad_register_brushes(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("bob_tick", ((qa_q1ad_handlers){.action = bob_tick}));
  REG("bob_think", ((qa_q1ad_handlers){.action = bob_think}));
  REG("bob_use", ((qa_q1ad_handlers){.use = bob_use}));
  REG("bob_blocked", ((qa_q1ad_handlers){.blocked = bob_blocked}));
  REG("toss_think", ((qa_q1ad_handlers){.action = toss_think}));
  REG("toss", ((qa_q1ad_handlers){.action = toss_action}));
  REG("toss_use", ((qa_q1ad_handlers){.use = toss_use}));
  REG("toss_cascade", ((qa_q1ad_handlers){.action = toss_cascade}));
  REG("shatter", ((qa_q1ad_handlers){.use = shatter_use}));
  REG("debris_fade", ((qa_q1ad_handlers){.action = debris_fade}));
  REG("debris_wake", ((qa_q1ad_handlers){.action = debris_wake}));
  REG("debris_wait", ((qa_q1ad_handlers){.use = debris_wait}));
  REG("explode", ((qa_q1ad_handlers){.action = explode_action}));
  REG("explode_die", ((qa_q1ad_handlers){.die = explode_die}));
  REG("hurt_use", ((qa_q1ad_handlers){.use = hurt_use}));
  REG("hurt_touch", ((qa_q1ad_handlers){.touch = hurt_touch}));
  REG("model_loop", ((qa_q1ad_handlers){.action = model_loop}));
  REG("model_once", ((qa_q1ad_handlers){.action = model_once}));
  REG("model_use_once", ((qa_q1ad_handlers){.use = model_use_once}));
  REG("model_use_loop", ((qa_q1ad_handlers){.use = model_use_loop}));
  REG("rotate_tick", ((qa_q1ad_handlers){.action = rotate_tick}));
  REG("rotate_tween", ((qa_q1ad_handlers){.action = rotate_tween}));
  REG("rotate_use", ((qa_q1ad_handlers){.use = rotate_use}));
  qa_q1ad_register_spawn(context, "func_bob", false, spawn_bob);
  qa_q1ad_register_spawn(context, "func_toss", false, spawn_toss);
  qa_q1ad_register_spawn(context, "func_shatter", false, spawn_shatter);
  qa_q1ad_register_spawn(context, "func_debris", false, spawn_debris);
  qa_q1ad_register_spawn(context, "func_explode", false, spawn_explode);
  qa_q1ad_register_spawn(context, "func_hurt", false, spawn_hurt);
  qa_q1ad_register_spawn(context, "func_fade", false, spawn_fade_brush);
  qa_q1ad_register_spawn(context, "misc_model", false, spawn_model);
  qa_q1ad_register_spawn(context, "info_rotate_axis", false, spawn_remove);
  qa_q1ad_register_spawn(context, "rotate_object_continuously", false, spawn_rotate);
  if (context->program == QA_Q1AD_MG3) {
    REG("breakable_stop", ((qa_q1ad_handlers){.action = breakable_stop}));
    REG("breakable_pain", ((qa_q1ad_handlers){.pain = breakable_pain}));
    REG("breakable_die", ((qa_q1ad_handlers){.die = breakable_die}));
    qa_q1ad_register_spawn(context, "func_breakable", false, spawn_breakable);
  }
#undef REG
}
