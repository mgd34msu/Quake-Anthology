/* Port of src/content/q1/missionpacks/world/hipnotic-train.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_train.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||` on a number */

static void train_next(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const double current = qa_q1_actor_number(entity, "cnt");
  qa_q1_actor *corner = qa_q1mp_find_first(game, entity->target);
  if (corner == NULL) qa_range_error("hip_train_next: missing %s", entity->target);
  qa_q1mp_world_number(entity, "cnt", corner->speed);
  qa_q1mp_set_string(&entity->target, corner->target);
  if (entity->target[0] == '\0') qa_range_error("hip_train_next: no next target");
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise1"), "voice", 1, 1);
  entity->wait = corner->wait;
  qa_actor_id prior_id;
  qa_q1_actor *prior = qa_q1_actor_reference_get(entity, "goalentity", &prior_id) ? qa_q1_get_entity(game, &prior_id) : NULL;
  if (prior != NULL && qa_q1_actor_text(prior, "event")[0] != '\0')
    qa_q1mp_world_target_event(game, entity, qa_q1_actor_text(prior, "event"), prior->message);
  if (!qa_q1_actor_reference_set(entity, "goalentity", &corner->actor.id)) qa_range_error("Out of memory");
  const char *next = corner->wait != 0 ? "hip:train_wait" : "hip:train_next";
  qa_vec3 a = qa_q1_get_body(game, corner).origin;
  qa_vec3 b = qa_q1_get_body(game, entity).bounds.min;
  const qa_vec3 destination = qa_q1_vsub(a, b);
  if (current == -1) {
    qa_q1_set_origin(game, entity, destination);
    double due_seconds = qa_q1mp_fround(qa_q1_actor_number(entity, "ltime") + 0.01);
    qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, entity, next);
    qa_q1_schedule_at(game, entity, due_seconds, callback);
    return;
  }
  if (current > 0) entity->speed = current;
  qa_q1_calc_move(game, entity, destination, entity->speed, qa_q1_callback_registry_action(game->named, entity, next));
}
static void train_find(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  double due_seconds;
  qa_q1_bound_action callback;
  (void)ctx;
  qa_q1_actor *corner = qa_q1mp_find_first(game, e->target);
  if (corner == NULL) qa_range_error("hip_func_train_find: missing %s", e->target);
  if (!qa_q1_actor_reference_set(e, "goalentity", &corner->actor.id)) qa_range_error("Out of memory");
  qa_q1mp_world_number(e, "cnt", corner->speed);
  qa_q1mp_set_string(&e->target, corner->target);
  qa_vec3 a = qa_q1_get_body(game, corner).origin;
  qa_vec3 b = qa_q1_get_body(game, e).bounds.min;
  qa_q1_set_origin(game, e, qa_q1_vsub(a, b));
  if (e->targetname[0] == '\0')
    (due_seconds = qa_q1mp_fround(qa_q1_actor_number(e, "ltime") + 0.1), callback = qa_q1_callback_registry_action(game->named, e, "hip:train_next"), qa_q1_schedule_at(game, e, due_seconds, callback));
}
static void train_wait(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  if (e->wait != 0) {
    qa_q1_sound(game, e->actor.id, qa_q1_actor_text(e, "noise"), "voice", 1, 1);
    if (e->wait == -1) return;
    const double wait = e->wait; e->wait = 0;
    double due_seconds2 = qa_q1mp_fround(qa_q1_actor_number(e, "ltime") + wait);
    qa_q1_bound_action callback2 = qa_q1_callback_registry_action(game->named, e, "hip:train_next");
    qa_q1_schedule_at(game, e, due_seconds2, callback2);
    return;
  }
  double due_seconds = qa_q1mp_fround(qa_q1_actor_number(e, "ltime") + 0.1);
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, e, "hip:train_next");
  qa_q1_schedule_at(game, e, due_seconds, callback);
}
static void train_use(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  const qa_vec3 velocity = qa_q1_get_body(game, e).velocity;
  if (velocity.x != 0 || velocity.y != 0 || velocity.z != 0) return;
  e->has_activator = activator != NULL;
  if (activator != NULL) e->activator = *activator;
  train_next(ctx, game, e);
}
static void train_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other) {
  (void)ctx;
  if (e->attack_finished > game->time) return;
  e->attack_finished = game->time + 0.5;
  (void)qa_q1_damage(game, other, e->actor.id, &e->actor.id, e->damage, NULL, "direct", "crush", NULL);
}
static void train2_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  if (e->target[0] == '\0') qa_range_error("func_train2 without a target");
  if (falsy(e->speed)) e->speed = 100;
  if (falsy(e->damage)) e->damage = 2;
  qa_q1mp_world_brush(game, e);
  qa_q1mp_world_number(e, "cnt", 1);
  if (qa_q1_actor_text(e, "noise")[0] == '\0') qa_q1mp_field_text(e, "noise", e->sounds == 1 ? "plats/train2.wav" : "misc/null.wav");
  if (qa_q1_actor_text(e, "noise1")[0] == '\0') qa_q1mp_field_text(e, "noise1", e->sounds == 1 ? "plats/train1.wav" : "misc/null.wav");
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:train_use");
  e->blocked = qa_q1_callback_registry_blocked(game->named, e, "hip:train_blocked");
  double due_seconds = qa_q1mp_fround(qa_q1_actor_number(e, "ltime") + 0.1);
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, e, "hip:train_find");
  qa_q1_schedule_at(game, e, due_seconds, callback);
}
static void bobbing_water(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  e->count = qa_q1mp_fround(e->count + e->speed * (game->time - qa_q1_actor_number(e, "ltime")));
  if (e->count > 360) e->count -= 360;
  qa_vec3 origin = qa_q1_get_body(game, e).origin;
  origin.z = qa_q1mp_fround(qa_q1_vectors(QA_Q1MP_V3(e->count, 0, 0)).forward.z * qa_q1_actor_number(e, "cnt"));
  qa_q1_set_origin(game, e, origin);
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1mp_world_later(game, e, 0.02, "hip:bobbing_water");
}
static void bobbing_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  e->solid = "none"; e->movement = "step"; e->count = 0;
  e->speed = 360 / (falsy(e->speed) ? 4 : e->speed);
  double left = qa_q1_get_body(game, e).bounds.max.z;
  double right = qa_q1_get_body(game, e).bounds.min.z;
  qa_q1mp_world_number(e, "cnt", (left - right) / 2);
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1_body_patch patch = {0}; patch.has_angles = true; patch.angles = QA_Q1_ZERO; qa_q1_set_body(game, e, &patch);
  qa_q1mp_world_later(game, e, 0.02, "hip:bobbing_water");
}
static void pushable_touch(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  qa_vec3 a;
  qa_vec3 b;
  (void)ctx; (void)normal; (void)surface;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  qa_q1_actor *owner = qa_q1_get_entity(game, e->has_owner ? &e->owner : NULL);
  if (!body.has_value || owner == NULL) return;
  const qa_vec3 velocity = body.value.velocity;
  const double yaw = fabs(velocity.x) > fabs(velocity.y) ? (velocity.x > 0 ? 0 : 180) : (velocity.y > 0 ? 90 : 270);
  (void)game->host.walk_move(game->host.ctx, e->actor, yaw, 16 * game->frame_seconds);
  qa_vec3 a2 = qa_q1_actor_vector(owner, "oldorigin");
  qa_vec3 b2 = (a = qa_q1_get_body(game, e).origin, b = qa_q1_actor_vector(e, "oldorigin"), qa_q1_vsub(a, b));
  qa_q1_set_origin(game, owner, qa_q1_vadd(a2, b2));
}
static void pushable_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_brush(game, e);
  const qa_q1_body body = qa_q1_get_body(game, e);
  qa_q1mp_world_vector(e, "oldorigin", body.origin);
  qa_q1_actor *proxy = qa_q1_create(game, "pushablewallproxy", NULL, NULL);
  proxy->has_owner = true; proxy->owner = e->actor.id; proxy->solid = "bbox"; proxy->movement = "step";
  const qa_vec3 size = qa_q1_vscale(qa_q1_vsub(body.bounds.max, body.bounds.min), 0.5);
  const qa_vec3 origin = qa_q1_vadd(qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5), QA_Q1MP_V3(0, 0, 1));
  qa_q1mp_world_vector(proxy, "oldorigin", origin);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_bounds = true; patch.bounds = (qa_bounds){qa_q1_vsub(QA_Q1MP_V3(-1, -1, 0), size), qa_q1_vadd(QA_Q1MP_V3(1, 1, -2), size)};
  qa_q1_set_body(game, proxy, &patch);
  proxy->touch = qa_q1_callback_registry_touch(game->named, proxy, "hip:pushable_touch");
  qa_q1_link(game, proxy);
}
void qa_q1mp_register_hipnotic_train(qa_q1_services *game) {
  qa_q1_callback_registry *named = game->named;
  qa_q1_callback_registry_register(named, "hip:train_next", (qa_q1_callback_handlers){.action = train_next});
  qa_q1_callback_registry_register(named, "hip:train_find", (qa_q1_callback_handlers){.action = train_find});
  qa_q1_callback_registry_register(named, "hip:train_wait", (qa_q1_callback_handlers){.action = train_wait});
  qa_q1_callback_registry_register(named, "hip:train_use", (qa_q1_callback_handlers){.use = train_use});
  qa_q1_callback_registry_register(named, "hip:train_blocked", (qa_q1_callback_handlers){.blocked = train_blocked});
  qa_q1_register_spawn(game, "func_train2", train2_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:bobbing_water", (qa_q1_callback_handlers){.action = bobbing_water});
  qa_q1_register_spawn(game, "func_bobbingwater", bobbing_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:pushable_touch", (qa_q1_callback_handlers){.touch = pushable_touch});
  qa_q1_register_spawn(game, "func_pushable", pushable_spawn, NULL);
}
