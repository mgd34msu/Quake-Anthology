/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/observations.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static qa_vec3 fr3(qa_vec3 v) { return (qa_vec3){(float)v.x, (float)v.y, (float)v.z}; }
static qa_aas_entity_info_bots_a empty_info(int64_t n) {
  qa_aas_entity_info_bots_a r;
  memset(&r, 0, sizeof r);
  r.number = n;
  return r;
}
bool qa_bot_entity_observations_init(qa_bot_entity_observations *o, int64_t cap) {
  o->capacity = cap;
  o->records = NULL;
  if (cap > 0) {
    o->records = malloc((size_t)cap * sizeof *o->records);
    if (!o->records)
      return false;
    for (int64_t i = 0; i < cap; i++)
      o->records[i] = empty_info(i);
  }
  return true;
}
void qa_bot_entity_observations_destroy(qa_bot_entity_observations *o) {
  free(o->records);
  o->records = NULL;
  o->capacity = 0;
}
void qa_bot_entity_observations_invalidate(qa_bot_entity_observations *o) {
  for (int64_t i = 0; i < o->capacity; i++)
    o->records[i].valid = false;
}
void qa_bot_entity_observations_update(qa_bot_entity_observations *o, int64_t n,
                                       const qa_bot_entity_update *s, double time) {
  if (n < 0 || n >= o->capacity)
    qa_range_error("Bot observation %lld exceeds source capacity %lld", (long long)n,
                   (long long)o->capacity);
  if (!s)
    return;
  qa_aas_entity_info_bots_a prev = o->records[n];
  bool gen_diff = (prev.has_generation != s->has_generation) ||
                  (prev.has_generation && prev.generation != s->generation);
  if (gen_diff)
    prev = empty_info(n);
  qa_aas_entity_info_bots_a r = empty_info(n);
  r.valid = true;
  r.has_generation = s->has_generation;
  r.generation = s->generation;
  r.type = s->type;
  r.flags = s->flags;
  r.ground_entity = s->ground_entity;
  r.solid = s->solid;
  r.model_index = s->model_index;
  r.model_index2 = s->model_index2;
  r.frame = s->frame;
  r.event = s->event;
  r.event_parameter = s->event_parameter;
  r.powerups = s->powerups;
  r.weapon = s->weapon;
  r.legs_animation = s->legs_animation;
  r.torso_animation = s->torso_animation;
  r.origin = fr3(s->origin);
  r.angles = fr3(s->angles);
  r.old_origin = fr3(s->old_origin);
  r.mins = fr3(s->mins);
  r.maxs = fr3(s->maxs);
  r.last_visible_origin = prev.origin;
  r.last_update_time = (float)time;
  r.update_interval = (float)((double)(float)time - (double)prev.last_update_time);
  o->records[n] = r;
}
qa_aas_entity_info_bots_a qa_bot_entity_observations_info(const qa_bot_entity_observations *o,
                                                          int64_t n) {
  if (n < 0 || n >= o->capacity)
    return empty_info(n);
  return o->records[n];
}
int64_t qa_bot_entity_observations_next_entity(const qa_bot_entity_observations *o, int64_t after) {
  for (int64_t i = after + 1; i < o->capacity; i++)
    if (o->records[i].valid)
      return i;
  return 0;
}
