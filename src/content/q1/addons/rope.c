/* Port of src/content/q1/addons/rope.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/rope.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

#define TICK "mg3:rope:tick"

static qa_q1ad_actor *next_segment(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_actor_id id;
  return qa_q1ad_actor_ref(entity, "rope.chain", &id) ? qa_q1ad_entity(context, &id) : NULL;
}
static void set_chain(qa_q1ad_actor *entity, qa_q1ad_actor *next) {
  qa_q1ad_actor_set_ref(entity, "rope.chain", next != NULL ? &next->actor.id : NULL);
}
static void rope_tick(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_vec3 origin = qa_q1ad_actor_vector(entity, "oldorigin");
  qa_actor_id self = entity->actor.id;
  qa_bounds point = {{0, 0, 0}, {0, 0, 0}};
  qa_q1ad_trace top = {.fraction = 1, .end = qa_add3(origin, (qa_vec3){0, 0, 2048})};
  qa_q1ad_trace bottom = {.fraction = 1, .end = qa_add3(origin, (qa_vec3){0, 0, -2048})};
  if (context->game.trace != NULL) {
    (void)context->game.trace(context->game.ctx, origin, top.end, point, &self, false, &top);
    (void)context->game.trace(context->game.ctx, origin, bottom.end, point, &self, false, &bottom);
  }
  double segments = floor(qa_length3(qa_sub3(bottom.end, top.end)) / 32);
  double models_d = floor(segments / 4);
  if (models_d < 0) models_d = 0;
  if (models_d > 32) models_d = 32;
  double models = models_d;
  while (models < entity->count) {
    qa_q1ad_actor *first = next_segment(context, entity);
    qa_q1ad_actor *following = first != NULL ? next_segment(context, first) : NULL;
    if (first != NULL && first->classname != NULL &&
        strcmp(first->classname, "misc_rope_segment") == 0) qa_q1ad_remove(context, first);
    set_chain(entity, following);
    entity->count--;
  }
  while (models > entity->count) {
    if (context->game.create == NULL) break;
    qa_q1ad_actor *segment = context->game.create(context->game.ctx, "misc_rope_segment");
    if (segment == NULL) break;
    segment->model = "progs/ropex.mdl";
    segment->frame = 3;
    segment->skin = entity->skin;
    segment->solid = "none";
    segment->damageable = false;
    segment->movement = "none";
    /* Source sets the parent bounds here, then sets its final bounds below. */
    qa_q1ad_set_bounds(context, entity, (qa_bounds){{-4, -4, 0}, {4, 4, 128}});
    set_chain(segment, next_segment(context, entity));
    set_chain(entity, segment);
    entity->count++;
  }
  qa_q1ad_actor *segment = next_segment(context, entity);
  qa_vec3 position = bottom.end;
  for (double index = 0; index < models; index++) {
    if (segment == NULL) qa_range_error("misc_rope lost its source segment chain");
    qa_q1ad_set_origin(context, segment, position);
    position = qa_add3(position, (qa_vec3){0, 0, 128});
    segment = next_segment(context, segment);
    segments -= 4;
  }
  qa_q1ad_set_origin(context, entity, position);
  entity->frame = segments;
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-4, -4, 0}, {4, 4, 32 * (segments + 1)}});
}
static void spawn_rope(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->model = "progs/ropex.mdl";
  entity->skin = qa_q1ad_actor_number(entity, "skin");
  qa_q1ad_set_vector(context, entity, "oldorigin", qa_q1ad_body_of(context, entity).origin);
  entity->count = 0;
  entity->solid = "none";
  entity->damageable = false;
  entity->movement = "none";
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-4, -4, 0}, {4, 4, 32}});
  qa_q1ad_add_frame_tick(context, entity, TICK);
}

void qa_q1ad_register_ropes(qa_q1ad_context *context) {
  qa_q1ad_register_named(context, TICK, (qa_q1ad_handlers){.action = rope_tick});
  qa_q1ad_register_spawn(context, "misc_rope", false, spawn_rope);
}
