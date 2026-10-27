/* Port of src/content/q3/base/game/triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/triggers.h"
#include <math.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
static float f32(double v) { return (float)v; }
void qa_q3_aim_at_target(qa_q3_aim_at_target_context context, qa_q3_game_entity *entity, qa_vec3 origin) {
  qa_q3_game_entity *target = qa_q3_pick_target(context.selection, entity->target);
  if (!target) { qa_q3_entity_pool_free(context.selection.pool, entity); return; }
  double height = f32(target->s.origin.z - origin.z), gravity = f32(context.gravity(context.gravity_ctx));
  double time = f32(sqrt(f32(height / f32(f32(0.5) * gravity))));
  if (time == 0) { qa_q3_entity_pool_free(context.selection.pool, entity); return; }
  qa_vec3 offset = qa_sub3(target->s.origin, origin), horizontal = {offset.x, offset.y, 0};
  double distance = f32(sqrt(qa_dot3(horizontal, horizontal)));
  qa_vec3 direction = distance == 0 ? horizontal : qa_scale3(horizontal, f32(1 / distance));
  double forward = f32(distance / time); qa_vec3 velocity = qa_scale3(direction, forward);
  entity->s.origin2 = (qa_vec3){velocity.x, velocity.y, f32(time * gravity)};
}
static void unimplemented(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)ctx; (void)e; (void)v; qa_range_error("Q3 trigger spawn handler requires ServerWorld host wiring (unported)");
}
static const char *const NAMES[] = {"trigger_always", "trigger_multiple", "trigger_push", "trigger_teleport", "trigger_hurt", "func_timer"};
size_t qa_q3_trigger_spawn_handlers(qa_q3_trigger_host host, qa_q3_spawn_handler_entry *out, size_t out_cap) {
  size_t n = sizeof NAMES / sizeof *NAMES;
  for (size_t i = 0; i < n && i < out_cap; i++) { out[i].classname = NAMES[i]; out[i].fn = unimplemented; out[i].ctx = host.ctx; }
  return n;
}
void qa_q3_bind_trigger_save_callbacks(qa_q3_trigger_host host) { (void)host; }
