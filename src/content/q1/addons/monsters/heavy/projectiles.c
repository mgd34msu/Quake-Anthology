/* Port of src/content/q1/addons/monsters/heavy/projectiles.ts.
 * MG3 launch_spike and LightningDamage source operations. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/projectiles.h"

#include <string.h>

#include "qa/content/q1/addons/monsters/heavy/runtime.h"
#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

qa_q1_actor *qa_q1am_heavy_spike(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity) {
  qa_q1_actor *missile = qa_q1_launch_spike(game, &owner, origin, velocity, "spike");
  qa_q1ad_set_text(&missile->classname, "knightspike");
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, QA_Q1AM_HEAVY_PREFIX ":spike_touch");
  return missile;
}
static void spike_touch(void *ctx, qa_q1_services *runtime, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (runtime, entity, other) */
  const qa_q1_actor *target = qa_q1_get_entity(runtime, &other);
  if ((entity->has_owner && qa_same_actor(other, entity->owner)) || (target != NULL && strcmp(target->solid, "trigger") == 0))
    return;
  const qa_vec3 origin = qa_q1_get_body(runtime, entity).origin;
  if (strcmp(runtime->host.contents(runtime->host.ctx, origin), "sky") == 0) {
    qa_q1_remove(runtime, entity);
    return;
  }
  if (qa_q1am_can_take_damage(runtime, other)) {
    qa_q1_effect(runtime, "blood", origin, &other, 9);
    (void)qa_q1_damage(runtime, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 9, NULL, NULL, NULL, NULL);
  } else {
    qa_q1_effect(runtime, "knight-spike", origin, NULL, 1);
  }
  qa_q1_remove(runtime, entity);
}
void qa_q1am_register_heavy_projectiles(qa_q1_services *game) {
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = game;
  handlers.touch = spike_touch;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_HEAVY_PREFIX ":spike_touch", handlers);
}
void qa_q1am_heavy_lightning_damage(qa_q1_services *game, qa_actor_id actor, qa_vec3 start, qa_vec3 end, double damage) {
  const qa_vec3 delta = qa_sub3(end, start), side = {-delta.y * 16, -delta.y * 16, 0};
  const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_scale3(side, -1)};
  qa_actor_id hit[3];
  bool hit_has[3];
  for (size_t i = 0; i < 3; i++) {
    const qa_q1_trace trace = qa_q1am_trace(game, qa_add3(start, offsets[i]), qa_add3(end, offsets[i]), QA_Q1_POINT, &actor, true);
    bool seen = false;
    for (size_t j = 0; j < i && !seen; j++) seen = trace.has_actor && hit_has[j] && qa_same_actor(hit[j], trace.actor);
    if (trace.has_actor && !seen && qa_q1am_can_take_damage(game, trace.actor)) {
      qa_q1_event event;
      memset(&event, 0, sizeof event);
      event.kind = "particles";
      event.has_origin = true;
      event.origin = trace.end;
      event.direction = (qa_vec3){0, 0, 100};
      event.color = 246;
      event.count = damage * 4;
      qa_q1_emit(game, event);
      (void)qa_q1_damage(game, trace.actor, actor, &actor, damage, NULL, NULL, NULL, NULL);
    }
    hit_has[i] = trace.has_actor;
    if (trace.has_actor) hit[i] = trace.actor;
  }
}
