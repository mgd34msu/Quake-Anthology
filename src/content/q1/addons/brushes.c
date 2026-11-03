/* Port of src/content/q1/addons/brushes.ts. MG1/MG3 func_bob.qc,
 * func_toss.qc, rotate.qc, misc.qc, misc_model.qc. Copyright (C) 1996-2026
 * id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/brushes.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* `${program}:brush:` names: static literals, since slots borrow the name. */
#define NAMED(context, name) \
  ((const char *const[]){"dopa:brush:" name, "mg1:brush:" name, "mg3:brush:" name, \
                         "ctf:brush:" name}[(context)->program])

static double fr(double value) { return (double)(float)value; }
static bool falsy(double value) { return value == 0 || isnan(value); }
static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
static bool same(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }

static qa_q1_bound_action named_action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(game->named, entity, name);
}
static void schedule(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double delay) {
  qa_q1_schedule(context->game, entity, delay, named_action(context->game, entity, name));
}
static void schedule_at(qa_q1_services *game, qa_q1_actor *entity, double time, const char *name) {
  qa_q1_schedule_at(game, entity, time, named_action(game, entity, name));
}
static void set_velocity(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 velocity) {
  qa_q1_body_patch patch = {.has_velocity = true, .velocity = velocity};
  qa_q1_set_body(game, entity, &patch);
}
static void push_brush(qa_q1_services *game, qa_q1_actor *entity) {
  entity->solid = "bsp";
  entity->movement = "push";
  qa_q1_link(game, entity);
}
/* (game.host.random() * 2 - 1): one draw per call, in TS order at call sites. */
static double signed_random(qa_q1_services *game) { return qa_q1_host_random(game) * 2 - 1; }

static qa_vec3 bob_position(qa_q1_actor *entity, double angle) {
  entity->count = fr(fmod(fr(entity->count + angle), 360));
  /* The source sin()/cos() are QuakeC functions of degrees (makevectors on {0, angle, 0}). */
  double radians = entity->count * (3.141592653589793 * 2 / 360) /* Math.PI */;
  qa_vec3 a = qa_q1_vscale(qa_q1_actor_vector(entity, "dest"), fr(sin(radians)));
  qa_vec3 b = qa_q1_vscale(qa_q1_actor_vector(entity, "dest2"), fr(cos(radians)));
  return qa_q1_vadd(a, b);
}

static void bob_tick(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_set_origin(game, entity, bob_position(entity, fr(entity->angular_velocity.x * context->frame_time)));
}
static void bob_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_vec3 position = bob_position(entity, fr(entity->angular_velocity.x * 0.05));
  set_velocity(game, entity, qa_q1_vscale(qa_q1_vsub(position, qa_q1_get_body(game, entity).origin), 20));
  schedule_at(game, entity, fr(qa_q1_actor_number(entity, "ltime") + 0.05), NAMED(context, "bob_think"));
}
static void bob_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                    const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  bool solid = !same(entity->solid, "none");
  if (entity->activated) {
    if (solid) { set_velocity(game, entity, QA_Q1_ZERO); qa_q1_cancel(game, entity); }
    else qa_q1ad_remove_frame_tick(context, entity);
  } else if (solid) {
    qa_q1_call_action(game, entity, named_action(game, entity, NAMED(context, "bob_think")));
  } else {
    qa_q1ad_add_frame_tick(context, entity, NAMED(context, "bob_tick"));
  }
  entity->activated = !entity->activated;
}
static void bob_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx;
  if (entity->attack_finished > game->time) return;
  qa_actor_id self = entity->actor.id;
  (void)qa_q1_damage(game, other, self, &self, entity->damage, NULL, NULL, NULL, NULL);
  entity->attack_finished = fr(game->time + 0.5);
}
static void spawn_bob(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1_length(qa_q1_actor_vector(entity, "dest")) == 0) qa_q1ad_set_vector(context, entity, "dest", (qa_vec3){0, 0, 64});
  if (falsy(entity->wait)) entity->wait = 10;
  if (falsy(entity->damage)) entity->damage = 1;
  entity->angular_velocity = (qa_vec3){fr(360 / entity->wait), 0, 0};
  entity->count = fr(360 * entity->delay);
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "bob_use"));
  entity->blocked = qa_q1_callback_registry_blocked(game->named, entity, NAMED(context, "bob_blocked"));
  entity->solid = (spawnflags(entity) & 1) != 0 ? "none" : "bsp";
  entity->movement = same(entity->solid, "none") ? "none" : "push";
  if ((spawnflags(entity) & 2) != 0) qa_q1_call_use(game, entity, entity->use, NULL, NULL);
}

static void toss_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1_length(qa_q1_get_body(game, entity).velocity) < 0.01) {
    qa_vec3 a;
    qa_vec3 b;
    if ((spawnflags(entity) & 2) != 0 &&
        ((spawnflags(entity) & 8) == 0 ||
         qa_q1_length((a = qa_q1_actor_vector(entity, "oldorigin"), b = qa_q1_get_body(game, entity).origin, qa_q1_vsub(a, b))) > 64)) {
      qa_q1_remove(game, entity);
      return;
    }
    set_velocity(game, entity, QA_Q1_ZERO);
    push_brush(game, entity);
    return;
  }
  schedule(context, entity, NAMED(context, "toss_think"), 0.5);
}
static void toss_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_body body = qa_q1_get_body(game, entity);
  entity->solid = "bbox";
  entity->movement = (spawnflags(entity) & 4) != 0 ? "toss" : "bounce";
  qa_q1_body_patch patch = {.has_velocity = true, .velocity = entity->movedir, .has_bounds = true,
                            .bounds = {qa_q1_vadd(body.bounds.min, (qa_vec3){4, 4, 0}), body.bounds.max}};
  qa_q1_set_body(game, entity, &patch);
  if (qa_q1_actor_text(entity, "noise")[0] != '\0') qa_q1_sound_defaults(game, entity->actor.id, qa_q1_actor_text(entity, "noise"));
  qa_q1_link(game, entity);
  schedule(context, entity, NAMED(context, "toss_think"), 0.25);
}
static void toss_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  entity->use = (qa_q1_bound_use){0};
  if (entity->delay != 0)
    schedule_at(game, entity, fr(qa_q1_actor_number(entity, "ltime") + entity->delay), NAMED(context, "toss"));
  else
    qa_q1_call_action(game, entity, named_action(game, entity, NAMED(context, "toss")));
}
static void toss_cascade(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
  qa_vec3 position = qa_q1_vscale(qa_q1_vadd(bounds.min, bounds.max), 0.5);
  double nearest = 16384;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *other = game->entities[i];
    if (other == NULL || strcmp(qa_q1_actor_text(other, "netname"), "_toss_origin") != 0 ||
        strcmp(other->targetname, entity->targetname) != 0) continue;
    qa_bounds other_bounds = qa_q1_get_body(game, other).bounds;
    nearest = qa_js_min(nearest, fr(qa_q1_length(qa_q1_vsub(position, qa_q1_vscale(qa_q1_vadd(other_bounds.min, other_bounds.max), 0.5))) /
                                    other->speed));
  }
  if (nearest < 16384) entity->delay = nearest;
}
static void spawn_toss(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  push_brush(game, entity);
  qa_q1ad_set_vector(context, entity, "oldorigin", qa_q1_get_body(game, entity).origin);
  entity->movedir = qa_q1_actor_vector(entity, "movedir");
  if (qa_q1_length(entity->movedir) == 0) entity->movedir = (qa_vec3){0, 0, 200};
  qa_vec3 variance = qa_q1_actor_vector(entity, "dest");
  if (qa_q1_length(variance) != 0) {
    qa_vec3 jitter;
    jitter.x = signed_random(game) * variance.x;
    jitter.y = signed_random(game) * variance.y;
    jitter.z = signed_random(game) * variance.z;
    entity->movedir = qa_q1_vadd(entity->movedir, jitter);
  }
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "toss_use"));
  if ((spawnflags(entity) & 1) != 0) {
    (void)qa_q1_actor_field_set(entity, "netname", "_toss_origin");
    if (falsy(entity->speed)) entity->speed = 200;
    return;
  }
  if (entity->delay == 0)
    schedule_at(game, entity, fr(qa_q1_actor_number(entity, "ltime") + 0.2), NAMED(context, "toss_cascade"));
}
static void shatter_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  entity->use = (qa_q1_bound_use){0};
  entity->solid = "none";
  entity->movement = "toss";
  set_velocity(game, entity, entity->movedir);
  qa_q1_link(game, entity);
}
static void spawn_shatter(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  push_brush(game, entity);
  qa_q1_body body = qa_q1_get_body(game, entity);
  entity->pos1 = qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5));
  entity->pos2 = qa_q1_actor_vector(entity, "pos2");
  if (qa_q1_length(entity->pos2) == 0) entity->pos2 = qa_q1_vsub(body.origin, (qa_vec3){0, 0, 100});
  qa_vec3 difference = qa_q1_vsub(entity->pos1, entity->pos2);
  double distance = qa_q1_length(difference);
  entity->speed = fr(fr((falsy(entity->speed) ? 200 : entity->speed) * 10000) / fr(distance * distance));
  if (falsy(entity->wait)) entity->wait = 10;
  qa_vec3 jitter;
  jitter.x = signed_random(game) * entity->wait;
  jitter.y = signed_random(game) * entity->wait;
  jitter.z = signed_random(game) * entity->wait;
  entity->movedir = qa_q1_vadd(qa_q1_vscale(qa_q1_normalize(difference), entity->speed), jitter);
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "shatter"));
}
static void debris_fade(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  double alpha = fr(qa_q1_actor_number_or(entity, "alpha", 1) - fr(fr(1 / entity->wait) * context->frame_time));
  qa_q1ad_alpha(context, entity, alpha);
  if (alpha < 0) {
    qa_q1ad_alpha(context, entity, 1);
    qa_q1_set_origin(game, entity, qa_q1_actor_vector(entity, "oldorigin"));
    entity->movement = "toss";
    qa_vec3 velocity = {0, 0, 0};
    velocity.x = signed_random(game) * 8;
    velocity.y = signed_random(game) * 8;
    set_velocity(game, entity, velocity);
    schedule(context, entity, NAMED(context, "debris_fade"), entity->delay);
    return;
  }
  schedule(context, entity, NAMED(context, "debris_fade"), 0.01);
}
static void debris_wake(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->use = (qa_q1_bound_use){0};
  entity->solid = "none";
  entity->movement = "toss";
  qa_q1ad_alpha(context, entity, 1);
  qa_vec3 velocity = qa_q1_get_body(game, entity).velocity;
  velocity.x = signed_random(game) * 8;
  velocity.y = signed_random(game) * 8;
  set_velocity(game, entity, velocity);
  qa_q1_link(game, entity);
  schedule(context, entity, NAMED(context, "debris_fade"), entity->delay);
}
static void debris_wait(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  double ltime = qa_q1_actor_number(entity, "ltime");
  schedule_at(game, entity, fr(ltime + qa_q1_host_random(game)), NAMED(context, "debris_wake"));
}
static void spawn_debris(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  push_brush(game, entity);
  qa_q1ad_set_vector(context, entity, "oldorigin", qa_q1_get_body(game, entity).origin);
  if (falsy(entity->delay)) entity->delay = 1.5;
  if (falsy(entity->wait)) entity->wait = 0.1;
  entity->movedir = (qa_vec3){0, 0, 200};
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "debris_wait"));
  qa_q1ad_alpha(context, entity, 1);
}
static void explode_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 position = qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5);
  qa_q1ad_set_text(&entity->model, "");
  qa_q1_set_origin(game, entity, position);
  qa_actor_id self = entity->actor.id;
  qa_q1_radius_damage(game, self, &self, 160, NULL, NULL, "");
  qa_q1_sound_defaults(game, self, "weapons/r_exp3.wav");
  qa_q1_effect(game, "explosion", position, NULL, 1);
  qa_q1_use_targets(game, entity, entity->has_activator ? &entity->activator : NULL);
  qa_q1_remove(game, entity);
}
static void explode_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  qa_q1ad_context *context = ctx;
  qa_q1_actor_set_damageable(entity, false);
  entity->has_activator = attacker != NULL;
  if (attacker != NULL) entity->activator = *attacker;
  schedule_at(game, entity, fr(qa_q1_actor_number(entity, "ltime") + 0.15), NAMED(context, "explode"));
}
static void spawn_explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  push_brush(game, entity);
  qa_q1_body_patch patch = {.has_angles = true, .angles = QA_Q1_ZERO};
  qa_q1_set_body(game, entity, &patch);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 20);
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->die = qa_q1_callback_registry_die(game->named, entity, NAMED(context, "explode_die"));
}
static void hurt_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  entity->activated = !entity->activated;
  entity->attack_finished = 0;
}
static void hurt_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!entity->activated || entity->attack_finished > game->time || qa_q1_health(game, other) <= 0) return;
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, other);
  if (!combat.has_value || !combat.value.can_take_damage) return;
  if (!qa_q1_is_player(game, &other) && !context->services.is_monster(context->services.ctx, other)) return;
  qa_actor_id world;
  if (game->world != NULL) world = game->world->actor.id;
  (void)qa_q1_damage(game, other, entity->actor.id, game->world != NULL ? &world : NULL, entity->damage, NULL, NULL, NULL, NULL);
  entity->attack_finished = fr(game->time + entity->wait);
}
static void spawn_hurt(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  push_brush(game, entity);
  if (falsy(entity->damage)) entity->damage = 10;
  if (falsy(entity->wait)) entity->wait = 0.2;
  entity->activated = (spawnflags(entity) & 1) != 0;
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "hurt_use"));
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "hurt_touch"));
}
/* The official function unconditionally delegates to func_wall before its obsolete fade code. */
static void spawn_fade_brush(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "func_wall_use");
  push_brush(game, entity);
}
static void model_loop(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  entity->frame++;
  if (entity->frame == qa_q1_actor_number(entity, "cnt")) entity->frame = entity->count;
  schedule(context, entity, NAMED(context, "model_loop"), 0.1);
}
static void model_once(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  entity->frame++;
  if (entity->frame < qa_q1_actor_number(entity, "cnt")) schedule(context, entity, NAMED(context, "model_once"), 0.1);
}
static void model_use_once(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                           const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  entity->frame = entity->count;
  schedule(context, entity, NAMED(context, "model_once"), 0.1);
}
static void model_use_loop(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                           const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  entity->spawnflags = (double)(spawnflags(entity) ^ 4);
  if ((spawnflags(entity) & 4) == 0) qa_q1_call_action(game, entity, named_action(game, entity, NAMED(context, "model_loop")));
  else qa_q1_cancel(game, entity);
}
static void spawn_model(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (entity->model[0] == '\0') qa_range_error("misc_model requires a model");
  entity->frame = qa_q1_actor_number(entity, "frame");
  entity->solid = "none";
  if (entity->spawnflags == 0) return;
  if (qa_q1_actor_number(entity, "cnt") < entity->frame || entity->targetname[0] == '\0')
    qa_range_error("misc_model requires a valid frame range and targetname");
  entity->count = entity->frame;
  if ((spawnflags(entity) & 2) != 0) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "model_use_once"));
  } else if ((spawnflags(entity) & 5) != 0) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "model_use_loop"));
    entity->spawnflags = (double)(spawnflags(entity) ^ 4);
    qa_q1_call_use(game, entity, entity->use, NULL, NULL);
  }
}
static void spawn_remove(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_remove(game, entity);
}
static double wrap_angle(double value) {
  if (value > 360) return value - ceil((value - 360) / 360) * 360;
  if (value < 0) return value + ceil(-value / 360) * 360;
  return value;
}
static void rotate(qa_q1ad_context *context, qa_q1_actor *entity, double scale) {
  qa_q1_services *game = context->game;
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 angles = qa_q1_vadd(body.angles, qa_q1_vscale(entity->angular_velocity, fr(context->frame_time * scale)));
  /* Source ModAngles mistakenly tests x for the negative-z adjustment. */
  qa_q1_body_patch patch = {.has_angles = true,
                            .angles = {wrap_angle(angles.x), wrap_angle(angles.y), angles.z > 360 ? wrap_angle(angles.z) : angles.z}};
  qa_q1_set_body(game, entity, &patch);
}
static void rotate_tick(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  rotate(ctx, entity, 1);
}
static void rotate_tween(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  entity->speed = qa_js_min(1, qa_js_max(0, fr(entity->speed + fr(qa_q1_actor_number(entity, "distance") * context->frame_time))));
  if (entity->speed == 0) {
    qa_q1ad_set_number(context, entity, "rotate.state", 0);
    qa_q1ad_remove_frame_tick(context, entity);
    return;
  }
  if (entity->speed == 1) {
    qa_q1ad_set_number(context, entity, "rotate.state", 2);
    (void)qa_q1_actor_field_set(entity, "addon.frameTick", NAMED(context, "rotate_tick"));
    rotate(context, entity, 1);
    return;
  }
  rotate(context, entity, entity->speed);
}
static void rotate_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                       const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  double state = qa_q1_actor_number(entity, "rotate.state");
  if (entity->delay <= 0) {
    qa_q1ad_set_number(context, entity, "rotate.state", state == 0 ? 2 : 0);
    if (state == 0) qa_q1ad_add_frame_tick(context, entity, NAMED(context, "rotate_tick"));
    else qa_q1ad_remove_frame_tick(context, entity);
    return;
  }
  (void)qa_q1_actor_field_set(entity, "addon.frameTick", NAMED(context, "rotate_tween"));
  bool forward = state == 0 || state == 3;
  qa_q1ad_set_number(context, entity, "rotate.state", forward ? 1 : 3);
  qa_q1ad_set_number(context, entity, "distance", fabs(qa_q1_actor_number(entity, "distance")) * (forward ? 1 : -1));
  if (state == 0) qa_q1ad_add_frame_tick(context, entity, NAMED(context, "rotate_tween"));
}
static void spawn_rotate(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->angular_velocity = qa_q1_actor_vector(entity, "avelocity");
  if (qa_q1_length(entity->angular_velocity) == 0) entity->angular_velocity = (qa_vec3){0, 30, 0};
  if (entity->delay > 0) qa_q1ad_set_number(context, entity, "distance", 1 / entity->delay);
  entity->solid = (spawnflags(entity) & 4) != 0 ? "bsp" : "none";
  entity->movement = same(entity->solid, "bsp") ? "push" : "none";
  qa_q1_body_patch patch = {.has_angles = true, .angles = QA_Q1_ZERO};
  qa_q1_set_body(game, entity, &patch);
  qa_vec3 pos = qa_q1_actor_vector(entity, "pos2");
  if (qa_q1_length(pos) != 0) qa_q1_set_origin(game, entity, pos);
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "rotate_use"));
  bool off = (spawnflags(entity) & 1) != 0;
  qa_q1ad_set_number(context, entity, "rotate.state", off ? 0 : 2);
  if (!off) {
    entity->speed = 1;
    qa_q1ad_add_frame_tick(context, entity, NAMED(context, "rotate_tick"));
  }
}
static void breakable_stop(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  set_velocity(game, entity, QA_Q1_ZERO);
}
static void breakable_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker,
                           double damage) {
  (void)attacker; (void)damage;
  qa_q1ad_context *context = ctx;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 10000);
  set_velocity(game, entity, (qa_vec3){0, 0, -20});
  schedule_at(game, entity, fr(qa_q1_actor_number(entity, "ltime") + 1), NAMED(context, "breakable_stop"));
}
static void breakable_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_remove(game, entity);
}
static void spawn_breakable(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  push_brush(game, entity);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 10000);
  entity->max_health = 10000;
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = false;
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, NAMED(context, "breakable_pain"));
  entity->die = qa_q1_callback_registry_die(game->named, entity, NAMED(context, "breakable_die"));
}

static void reg(qa_q1ad_context *context, const char *name, qa_q1_callback_handlers handlers) {
  handlers.ctx = context;
  qa_q1_callback_registry_register(context->game->named, name, handlers);
}

void qa_q1ad_register_brushes(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  reg(context, NAMED(context, "bob_tick"), (qa_q1_callback_handlers){.action = bob_tick});
  reg(context, NAMED(context, "bob_think"), (qa_q1_callback_handlers){.action = bob_think});
  reg(context, NAMED(context, "bob_use"), (qa_q1_callback_handlers){.use = bob_use});
  reg(context, NAMED(context, "bob_blocked"), (qa_q1_callback_handlers){.blocked = bob_blocked});
  qa_q1_register_spawn(game, "func_bob", spawn_bob, context);
  reg(context, NAMED(context, "toss_think"), (qa_q1_callback_handlers){.action = toss_think});
  reg(context, NAMED(context, "toss"), (qa_q1_callback_handlers){.action = toss_action});
  reg(context, NAMED(context, "toss_use"), (qa_q1_callback_handlers){.use = toss_use});
  reg(context, NAMED(context, "toss_cascade"), (qa_q1_callback_handlers){.action = toss_cascade});
  qa_q1_register_spawn(game, "func_toss", spawn_toss, context);
  reg(context, NAMED(context, "shatter"), (qa_q1_callback_handlers){.use = shatter_use});
  qa_q1_register_spawn(game, "func_shatter", spawn_shatter, context);
  reg(context, NAMED(context, "debris_fade"), (qa_q1_callback_handlers){.action = debris_fade});
  reg(context, NAMED(context, "debris_wake"), (qa_q1_callback_handlers){.action = debris_wake});
  reg(context, NAMED(context, "debris_wait"), (qa_q1_callback_handlers){.use = debris_wait});
  qa_q1_register_spawn(game, "func_debris", spawn_debris, context);
  reg(context, NAMED(context, "explode"), (qa_q1_callback_handlers){.action = explode_action});
  reg(context, NAMED(context, "explode_die"), (qa_q1_callback_handlers){.die = explode_die});
  qa_q1_register_spawn(game, "func_explode", spawn_explode, context);
  reg(context, NAMED(context, "hurt_use"), (qa_q1_callback_handlers){.use = hurt_use});
  reg(context, NAMED(context, "hurt_touch"), (qa_q1_callback_handlers){.touch = hurt_touch});
  qa_q1_register_spawn(game, "func_hurt", spawn_hurt, context);
  qa_q1_register_spawn(game, "func_fade", spawn_fade_brush, context);
  reg(context, NAMED(context, "model_loop"), (qa_q1_callback_handlers){.action = model_loop});
  reg(context, NAMED(context, "model_once"), (qa_q1_callback_handlers){.action = model_once});
  reg(context, NAMED(context, "model_use_once"), (qa_q1_callback_handlers){.use = model_use_once});
  reg(context, NAMED(context, "model_use_loop"), (qa_q1_callback_handlers){.use = model_use_loop});
  qa_q1_register_spawn(game, "misc_model", spawn_model, context);
  qa_q1_register_spawn(game, "info_rotate_axis", spawn_remove, context);
  reg(context, NAMED(context, "rotate_tick"), (qa_q1_callback_handlers){.action = rotate_tick});
  reg(context, NAMED(context, "rotate_tween"), (qa_q1_callback_handlers){.action = rotate_tween});
  reg(context, NAMED(context, "rotate_use"), (qa_q1_callback_handlers){.use = rotate_use});
  qa_q1_register_spawn(game, "rotate_object_continuously", spawn_rotate, context);
  if (context->program == QA_Q1AD_MG3) {
    reg(context, NAMED(context, "breakable_stop"), (qa_q1_callback_handlers){.action = breakable_stop});
    reg(context, NAMED(context, "breakable_pain"), (qa_q1_callback_handlers){.pain = breakable_pain});
    reg(context, NAMED(context, "breakable_die"), (qa_q1_callback_handlers){.die = breakable_die});
    qa_q1_register_spawn(game, "func_breakable", spawn_breakable, context);
  }
}
