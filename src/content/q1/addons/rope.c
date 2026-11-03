/* Port of src/content/q1/addons/rope.ts (quakec_mg3/misc_rope.qc).
 * Copyright (C) 1996-2026 id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/rope.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

#define TICK "mg3:rope:tick"

static qa_q1_actor *next_segment(qa_q1_services *game, qa_q1_actor *entity) {
  qa_actor_id id;
  return qa_q1_actor_reference_get(entity, "rope.chain", &id) ? qa_q1_get_entity(game, &id) : NULL;
}

static void rope_tick(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_vec3 origin = qa_q1_actor_vector(entity, "oldorigin");
  qa_q1_trace top = qa_q1_host_trace(game, (qa_q1_trace_request){
      .start = origin, .end = qa_q1_vadd(origin, (qa_vec3){0, 0, 2048}), .bounds = QA_Q1_POINT,
      .has_ignore = true, .ignore = entity->actor.id, .monsters = false});
  qa_q1_trace bottom = qa_q1_host_trace(game, (qa_q1_trace_request){
      .start = origin, .end = qa_q1_vadd(origin, (qa_vec3){0, 0, -2048}), .bounds = QA_Q1_POINT,
      .has_ignore = true, .ignore = entity->actor.id, .monsters = false});
  double segments = floor(qa_q1_length(qa_q1_vsub(bottom.end, top.end)) / 32);
  double models = qa_js_max(0, qa_js_min(32, floor(segments / 4)));
  while (models < entity->count) {
    qa_q1_actor *first = next_segment(game, entity);
    qa_q1_actor *following = first == NULL ? NULL : next_segment(game, first);
    if (first != NULL && strcmp(first->classname, "misc_rope_segment") == 0) qa_q1_remove(game, first);
    (void)qa_q1_actor_reference_set(entity, "rope.chain", following != NULL ? &following->actor.id : NULL);
    entity->count--;
  }
  while (models > entity->count) {
    qa_q1_actor *segment = qa_q1_create(game, "misc_rope_segment", NULL, NULL);
    qa_q1ad_set_text(&segment->model, "progs/ropex.mdl");
    segment->frame = 3;
    segment->skin = entity->skin;
    segment->solid = "none";
    qa_q1_actor_set_damageable(segment, false);
    segment->movement = "none";
    /* Source sets the parent bounds here, then sets its final bounds below. */
    qa_q1_set_bounds(game, entity, (qa_bounds){{-4, -4, 0}, {4, 4, 128}});
    /* entity.references.get("rope.chain") ?? null: the stored id, live or not. */
    qa_actor_id chain;
    bool has_chain = qa_q1_actor_reference_get(entity, "rope.chain", &chain);
    (void)qa_q1_actor_reference_set(segment, "rope.chain", has_chain ? &chain : NULL);
    (void)qa_q1_actor_reference_set(entity, "rope.chain", &segment->actor.id);
    entity->count++;
  }
  qa_q1_actor *segment = next_segment(game, entity);
  qa_vec3 position = bottom.end;
  for (double index = 0; index < models; index++) {
    if (segment == NULL) qa_range_error("misc_rope lost its source segment chain");
    qa_q1_set_origin(game, segment, position);
    position = qa_q1_vadd(position, (qa_vec3){0, 0, 128});
    segment = next_segment(game, segment);
    segments -= 4;
  }
  qa_q1_set_origin(game, entity, position);
  entity->frame = segments;
  qa_q1_set_bounds(game, entity, (qa_bounds){{-4, -4, 0}, {4, 4, 32 * (segments + 1)}});
}

static void spawn_rope(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_set_text(&entity->model, "progs/ropex.mdl");
  entity->skin = qa_q1_actor_number(entity, "skin");
  qa_q1ad_set_vector(context, entity, "oldorigin", qa_q1_get_body(game, entity).origin);
  entity->count = 0;
  entity->solid = "none";
  qa_q1_actor_set_damageable(entity, false);
  entity->movement = "none";
  qa_q1_set_bounds(game, entity, (qa_bounds){{-4, -4, 0}, {4, 4, 32}});
  qa_q1ad_add_frame_tick(context, entity, TICK);
}

void qa_q1ad_register_ropes(qa_q1ad_context *context) {
  qa_q1_callback_registry_register(context->game->named, TICK, (qa_q1_callback_handlers){.ctx = context, .action = rope_tick});
  qa_q1_register_spawn(context->game, "misc_rope", spawn_rope, context);
}
