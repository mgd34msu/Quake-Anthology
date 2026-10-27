/* Port of src/content/q1/addons/monsters/heavy/projectiles.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/projectiles.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/heavy/runtime.h"

qa_q1am_actor *qa_q1am_heavy_spike(qa_q1am_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity) {
  qa_q1am_actor *missile = game->launch_spike(game->ctx, game, true, owner, origin, velocity);
  (void)snprintf(missile->classname, sizeof missile->classname, "%s", "knightspike");
  missile->touch = game->named_touch(game->ctx, missile, QA_Q1AM_HEAVY_PREFIX ":spike_touch");
  return missile;
}
static void spike_touch(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                        bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  if ((entity->has_owner && qa_same_actor(other, entity->owner)) ||
      (target && strcmp(target->solid, "trigger") == 0))
    return;
  qa_vec3 origin = game->body(game->ctx, entity).origin;
  if (strcmp(game->contents(game->ctx, origin), "sky") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (game->can_take_damage(game->ctx, other)) {
    game->effect(game->ctx, "blood", origin, true, other, 9);
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 9);
  } else {
    game->effect(game->ctx, "knight-spike", origin, false, (qa_actor_id){0}, 1);
  }
  game->remove(game->ctx, entity);
}
void qa_q1am_register_heavy_projectiles(qa_q1am_game *game) {
  qa_q1am_handlers handlers = {0};
  handlers.touch = spike_touch;
  game->named_register(game->ctx, QA_Q1AM_HEAVY_PREFIX ":spike_touch", handlers);
}
void qa_q1am_heavy_lightning_damage(qa_q1am_game *game, qa_actor_id actor, qa_vec3 start, qa_vec3 end, double damage) {
  qa_vec3 delta = qa_sub3(end, start);
  qa_vec3 side = {-delta.y * 16, -delta.y * 16, 0};
  qa_vec3 offsets[3] = {QA_Q1AM_ZERO, side, qa_scale3(side, -1)};
  bool hit_has[3] = {false, false, false};
  qa_actor_id hit[3];
  memset(hit, 0, sizeof hit);
  for (int i = 0; i < 3; i++) {
    qa_q1am_trace_request request = {qa_add3(start, offsets[i]), qa_add3(end, offsets[i]),
                                     QA_Q1AM_POINT, true, actor, true};
    qa_q1am_trace trace = game->trace(game->ctx, &request);
    bool seen = false;
    if (trace.has_actor)
      for (int p = 0; p < i; p++)
        if (hit_has[p] && qa_same_actor(hit[p], trace.actor)) seen = true;
    if (trace.has_actor && !seen && game->can_take_damage(game->ctx, trace.actor)) {
      qa_q1am_event event = {0};
      event.kind = "particles";
      event.origin = trace.end;
      event.direction = (qa_vec3){0, 0, 100};
      event.color = 246;
      event.count = damage * 4;
      game->emit(game->ctx, &event);
      game->damage(game->ctx, trace.actor, actor, true, actor, damage);
    }
    hit_has[i] = trace.has_actor;
    hit[i] = trace.actor;
  }
}
