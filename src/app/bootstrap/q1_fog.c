/* Port of src/app/bootstrap/q1-fog.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_fog.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_q1_map_fog {
  qa_q1_fog_state state;
  double sky_factor;
  bool has_owner;
  qa_presentation_owner owner;
  qa_q1_fog_transition initial;
  bool initial_enabled;
  const qa_content_id *contents;
  size_t content_count;
  qa_actor_id actor;
  bool enabled;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_app_q1_map_fog *qa_app_q1_map_fog_create(const char *entities,
    const qa_content_id *contents, size_t content_count, qa_actor_id actor, bool enabled) {
  qa_app_q1_map_fog *fog = calloc(1, sizeof *fog);
  if (fog == NULL) qa_range_error("Out of memory");
  qa_q1_fog_state_init(&fog->state);
  fog->sky_factor = 0.5;
  fog->contents = contents; fog->content_count = content_count;
  fog->actor = actor; fog->enabled = enabled; fog->initial_enabled = enabled;
  fog->initial = qa_q1_world_fog(entities);
  qa_q1_fog_state_install(&fog->state, fog->initial);
  return fog;
}
void qa_app_q1_map_fog_destroy(qa_app_q1_map_fog *fog) { free(fog); }
void qa_app_q1_map_fog_receive(qa_app_q1_map_fog *fog, const qa_app_q1_fog_event *events, size_t event_count) {
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_q1_fog_event *source = &events[i];
    if (is(source->kind, "presentation-owner") && is(source->event_kind, "retired")
        && qa_same_presentation_owner(fog->has_owner ? &fog->owner : NULL, source->owner)) {
      qa_q1_fog_state_install(&fog->state, fog->initial);
      fog->enabled = fog->initial_enabled; fog->sky_factor = 0.5; fog->has_owner = false;
    }
    if (!is(source->kind, "q1-fog")) continue;
    if (!source->has_owner) {
      bool known = false;
      for (size_t c = 0; c < fog->content_count; c++)
        if (strcmp(fog->contents[c], source->content) == 0) known = true;
      if (!known) continue;
    }
    if (source->has_player && !qa_same_actor(source->player, fog->actor)) continue;
    fog->enabled = true;
    fog->has_owner = source->has_owner;
    fog->owner = source->owner;
    qa_q1_fog_state_install(&fog->state, source->transition);
    fog->sky_factor = source->sky_factor;
  }
}
bool qa_app_q1_map_fog_active(const qa_app_q1_map_fog *fog) { return fog->enabled; }
static double byte_of(double value) {
  double clamped = value < 0 ? 0 : value > 1 ? 1 : value;
  return round(clamped * 255) / 255;
}
qa_scene_fog qa_app_q1_map_fog_current(qa_app_q1_map_fog *fog, double seconds) {
  qa_q1_fog value = qa_q1_fog_state_sample(&fog->state, seconds);
  qa_scene_fog out = {0};
  out.kind = "q1";
  out.density = value.density;
  out.color.x = byte_of(value.color.x);
  out.color.y = byte_of(value.color.y);
  out.color.z = byte_of(value.color.z);
  out.sky_factor = fog->sky_factor;
  return out;
}
