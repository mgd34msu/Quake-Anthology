/* Port of src/content/q3/base/game/item-motion.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/item_motion.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "qa/content/q3/base/game/ground.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
enum { ITEM_RADIUS = 15, EF_BOUNCE_HALF = 0x20, DROPPED_ITEM_LIFETIME = 30000 };
static double game_time(double v) {
  if (v != trunc(v) || v < -2147483648 || v > 2147483647) qa_range_error("Item motion time must be a signed 32-bit millisecond value");
  return v;
}
static double item_index(qa_q3_base_product pool_product, qa_q3_base_product product, const qa_q3_base_item_definition *item) {
  if (pool_product != product) qa_range_error("Item motion product does not match its entity pool");
  size_t n = 0; const qa_q3_base_item_definition *items = qa_q3_base_item_list(product, &n);
  for (size_t i = 0; i < n; i++) if (&items[i] == item) { if (i < 1) break; return (double)i; }
  qa_range_error("Launched item does not belong to the selected product table");
}
static void require_owned(qa_q3_entity_pool *p, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(p, e->slot) != e) qa_range_error("Item motion entity does not belong to its entity pool or was replaced");
}
static qa_vec3 collision_normal(qa_q3_base_actor_trace_result t) { qa_vec3 z = {0,0,0}; return t.contact_is_plane ? t.contact_plane.normal : z; }
void qa_q3_bounce_item(qa_q3_game_entity *entity, qa_q3_base_actor_trace_result trace, qa_q3_item_frame_time frame, qa_q3_entity_pool *entities) {
  double time = game_time(frame.time), previous = game_time(frame.previous_time);
  double fraction = (float)trace.fraction, elapsed = (float)(double)qa_int32(time - previous);
  double hit_time = qa_qvm_float_to_int((float)((float)previous + (float)(elapsed * fraction)));
  qa_vec3 velocity = qa_q3_base_evaluate_trajectory_delta(entity->s.pos, hit_time);
  qa_vec3 normal = collision_normal(trace); double dot = qa_dot3(velocity, normal);
  qa_vec3 reflected = qa_add3(velocity, qa_scale3(normal, (float)(-2 * dot)));
  qa_vec3 delta = qa_scale3(reflected, entity->physics_bounce); entity->s.pos.delta = delta;
  if (normal.z > 0 && delta.z < 40) {
    qa_vec3 stopped = {(double)qa_qvm_float_to_int(trace.end.x), (double)qa_qvm_float_to_int(trace.end.y), (double)qa_qvm_float_to_int((float)(trace.end.z + 1))};
    qa_q3_set_origin(entity, stopped);
    qa_q3_trace_ground(entity, trace.hit_kind, trace.hit_actor, entities); return;
  }
  qa_q3_base_entity_shared_set_current_origin(&entity->r, qa_add3(qa_q3_base_entity_shared_current_origin(&entity->r), normal));
  entity->s.pos.base = qa_q3_base_entity_shared_current_origin(&entity->r); entity->s.pos.time = time;
}
void qa_q3_run_item(qa_q3_game_entity *entity, qa_q3_run_item_context context) {
  require_owned(context.entities, entity);
  double time = game_time(context.frame.time); (void)game_time(context.frame.previous_time);
  if (entity->s.ground_entity_num == -1 && entity->s.pos.type != QA_Q3_BASE_TR_GRAVITY) { entity->s.pos.type = QA_Q3_BASE_TR_GRAVITY; entity->s.pos.time = time; }
  if (entity->s.pos.type == QA_Q3_BASE_TR_STATIONARY) { qa_q3_run_think(entity, time); return; }
  qa_vec3 destination = qa_q3_base_evaluate_trajectory(entity->s.pos, time);
  qa_q3_game_entity *owner = (entity->r.owner_num < 0 || entity->r.owner_num >= 1022) ? NULL : qa_q3_entity_pool_get(context.entities, entity->r.owner_num);
  qa_actor_id owner_id; const qa_actor_id *pass = NULL;
  if (owner && qa_q3_game_entity_inuse(owner)) { owner_id = qa_q3_game_entity_actor(owner).id; pass = &owner_id; }
  qa_q3_base_actor_trace_query query; memset(&query, 0, sizeof query);
  query.start = qa_q3_base_entity_shared_current_origin(&entity->r); query.end = destination;
  query.shape.kind = QA_Q3_BASE_SHAPE_BOX;
  query.shape.mins = qa_q3_base_entity_shared_mins(&entity->r); query.shape.maxs = qa_q3_base_entity_shared_maxs(&entity->r);
  query.pass_actor = pass; query.mask = entity->clipmask != 0 ? entity->clipmask : (0x1 | 0x10000);
  qa_q3_base_actor_trace_result trace = context.world.trace_actor(context.world.ctx, query);
  qa_q3_base_entity_shared_set_current_origin(&entity->r, trace.end);
  if (context.entities->options.link) context.entities->options.link(context.entities->options.ctx, entity);
  qa_q3_run_think(entity, time);
  /* The think may free the item (30-second expiry); writes to a freed record would allocate a fresh actor. */
  if (!qa_q3_game_entity_inuse(entity)) return;
  double fraction = trace.solidity == QA_Q3_BASE_SOLID_CLEAR ? trace.fraction : 0;
  if (fraction == 1) return;
  if ((qa_js_to_int32(context.world.point_contents(context.world.ctx, qa_q3_base_entity_shared_current_origin(&entity->r), -1)) & INT32_MIN) != 0) { /* CONTENTS_NODROP: JS & yields the int32 sign bit */
    if (entity->item && entity->item->type == 8 && context.free_team_entity.fn) {
      qa_q3_entity_think_fn fn; memcpy(&fn, &context.free_team_entity.fn, sizeof fn); fn(context.free_team_entity.ctx, entity);
    } else qa_q3_entity_pool_free(context.entities, entity);
    return;
  }
  trace.fraction = fraction; qa_q3_bounce_item(entity, trace, context.frame, context.entities);
}
static bool is_special_team_drop(qa_q3_base_product product, double game_type, const qa_q3_base_item_definition *item) {
  if (item->type != 8) return false;
  return game_type == 4 || (product == QA_Q3_BASE_PRODUCT_MISSIONPACK && game_type == 5);
}
static void launch_think(void *ctx, qa_q3_game_entity *self) { qa_q3_entity_pool_free((qa_q3_entity_pool *)ctx, self); }
void qa_q3_bind_launch_save_callbacks(qa_q3_launch_item_context context) {
  qa_q3_callback_family_intern(&context.entities->callbacks.touch, "q3.item.touch", context.touch_item);
  qa_q3_callback_family_intern(&context.entities->callbacks.think, "q3.item.droppedFlag", context.dropped_flag_think);
  qa_q3_entity_callback cb; qa_q3_entity_think_fn fn = launch_think; memcpy(&cb.fn, &fn, sizeof fn); cb.ctx = context.entities;
  qa_q3_callback_family_intern(&context.entities->callbacks.think, "q3.base.game.item-motion.launchItem.think", cb);
}
qa_q3_game_entity *qa_q3_launch_item(qa_q3_launch_item_context context, const qa_q3_base_item_definition *item, qa_vec3 origin, qa_vec3 velocity) {
  qa_q3_bind_launch_save_callbacks(context);
  double time = game_time(context.time);
  double index = item_index(context.entities->options.product, context.product, item);
  qa_q3_game_entity *dropped = qa_q3_entity_pool_spawn(context.entities);
  dropped->s.e_type = 2; dropped->s.modelindex = index; dropped->s.modelindex2 = 1;
  qa_q3_game_entity_set_classname(dropped, item->class_name); dropped->item = item;
  qa_q3_base_entity_shared_set_mins(&dropped->r, (qa_vec3){-ITEM_RADIUS, -ITEM_RADIUS, -ITEM_RADIUS});
  qa_q3_base_entity_shared_set_maxs(&dropped->r, (qa_vec3){ITEM_RADIUS, ITEM_RADIUS, ITEM_RADIUS});
  dropped->r.contents = (double)(int32_t)0x40000000;
  dropped->touch = qa_q3_callback_family_resolve(&context.entities->callbacks.touch, "q3.item.touch");
  qa_q3_set_origin(dropped, origin);
  dropped->s.pos.type = QA_Q3_BASE_TR_GRAVITY; dropped->s.pos.time = time; dropped->s.pos.delta = velocity;
  dropped->s.e_flags = qa_js_to_int32(dropped->s.e_flags) | EF_BOUNCE_HALF;
  qa_q3_game_entity_set_nextthink(dropped, qa_int32(time + DROPPED_ITEM_LIFETIME));
  if (is_special_team_drop(context.product, context.game_type, item)) {
    dropped->think = qa_q3_callback_family_resolve(&context.entities->callbacks.think, "q3.item.droppedFlag");
    qa_q3_entity_think_fn fn; memcpy(&fn, &context.check_dropped_team_item.fn, sizeof fn); if (fn) fn(context.check_dropped_team_item.ctx, dropped);
  } else dropped->think = qa_q3_callback_family_resolve(&context.entities->callbacks.think, "q3.base.game.item-motion.launchItem.think");
  dropped->flags = QA_Q3_GAME_FLAG_DROPPED_ITEM;
  if (context.entities->options.link) context.entities->options.link(context.entities->options.ctx, dropped);
  return dropped;
}
qa_q3_game_entity *qa_q3_drop_item(qa_q3_drop_item_context context, qa_q3_game_entity *entity, const qa_q3_base_item_definition *item, double angle) {
  require_owned(context.launch.entities, entity);
  qa_vec3 forward = qa_qvm_angle_vectors((qa_vec3){0, entity->s.apos.base.y + angle, entity->s.apos.base.z}).forward;
  qa_vec3 horizontal = qa_scale3(forward, 150); double random = context.random(context.random_ctx);
  if (!isfinite(random) || random < 0 || random > 1) qa_range_error("Drop_Item random value must be within [0, 1]");
  double crandom = (float)(2 * (float)((float)random - 0.5));
  double lift = (float)(200 + (float)(crandom * 50));
  qa_vec3 velocity = {horizontal.x, horizontal.y, (float)(horizontal.z + lift)};
  return qa_q3_launch_item(context.launch, item, entity->s.pos.base, velocity);
}
